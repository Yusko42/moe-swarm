#!/usr/bin/env python3
import contextlib
import http.client
import json
import pathlib
import signal
import socket
import subprocess
import sys
import time

build, model, output = (pathlib.Path(arg).resolve() for arg in sys.argv[1:])
output.mkdir(parents=True, exist_ok=True)
base = ["--split-mode", "layer", "--load-mode", "none", "-c", "8192", "-np", "1",
        "-b", "512", "-ub", "256", "--no-context-shift", "-fa", "on",
        "--cache-type-k", "q8_0", "--cache-type-v", "q8_0", "--threads", "4",
        "--host", "127.0.0.1", "--no-slots", "-lv", "4"]
profiles = {"normal": base + ["-ngl", "0"], "measurement": base + ["-ngl", "0", "--metrics"]}
results = {"profiles": profiles, "checks": {}}
checks = results["checks"]
prefix = "Continue the sequence of increasing integers: " + ", ".join(map(str, range(32)))
long_request = dict(prompt=prefix, n_predict=128, ignore_eos=True, temperature=0, cache_prompt=True)

def port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]

@contextlib.contextmanager
def process(name, args):
    with (output / (name + ".log")).open("w") as log:
        with subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT) as proc:
            try:
                yield proc
            finally:
                if proc.poll() is None:
                    proc.terminate()
                    try:
                        proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        proc.kill()

def request(number, path, body=None):
    with contextlib.closing(http.client.HTTPConnection("127.0.0.1", number, timeout=90)) as conn:
        conn.request("GET" if body is None else "POST", path,
                     body=None if body is None else json.dumps(body),
                     headers={"Content-Type": "application/json"})
        with conn.getresponse() as response:
            return response.status, response.read().decode()

def ready(number, proc, http):
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline and proc.poll() is None:
        try:
            if http:
                if request(number, "/health")[0] == 200:
                    return
            else:
                with socket.create_connection(("127.0.0.1", number), timeout=1):
                    return
        except OSError:
            pass
        time.sleep(0.1)
    raise RuntimeError("Readiness failed")

def completion(number, body):
    status, response = request(number, "/completion", body)
    assert status == 200, (status, response)
    return json.loads(response)

def active(number):
    status, metrics = request(number, "/metrics")
    assert status == 200
    return float(next(line.split()[-1] for line in metrics.splitlines()
                      if line.startswith("llamacpp:requests_processing ")))

def cancel(number, log):
    checks_before = log.stat().st_size
    samples = []
    with contextlib.closing(http.client.HTTPConnection("127.0.0.1", number, timeout=10)) as conn:
        conn.request("POST", "/completion", json.dumps(dict(long_request, stream=True)),
                     {"Content-Type": "application/json"})
        transport = conn.sock
        with conn.getresponse() as response:
            assert response.status == 200
            for _ in range(8):
                while not (line := response.readline()).startswith(b"data: "):
                    if not line:
                        raise RuntimeError("Unexpected stream end")
                assert not json.loads(line[6:])["stop"]
            assert active(number) == 1
            closed = time.monotonic()
            wall = time.time()
            transport.shutdown(socket.SHUT_RDWR)
    while time.monotonic() - closed < 5:
        running = active(number)
        samples.append([round(time.monotonic() - closed, 4), running])
        if running == 0:
            break
        time.sleep(0.01)
    with log.open() as stream:
        stream.seek(checks_before)
        evidence = [line.strip() for line in stream if "release" in line or "cancel" in line]
    reply = completion(number, dict(long_request, prompt=prefix + ",", n_predict=8))
    return dict(next_content=reply["content"], closed_unix=wall, stream_events=8, processing_after_close=samples,
                log=evidence, next=reply.get("timings"), next_stop=reply.get("stop_type"))

@contextlib.contextmanager
def server(name, flags):
    number = port()
    with process(name, [build / "llama-server", "-m", model, "--port", str(number), *flags]) as proc:
        ready(number, proc, True)
        checks.setdefault("health", {})[name] = 200
        yield number

def run():
    for name in ("llama-server", "ggml-rpc-server"):
        for flag in ("--help", "--version"):
            result = subprocess.run([build / name, flag], capture_output=True, timeout=10)
            (output / (name + flag + ".txt")).write_bytes(result.stdout + result.stderr)
            checks[name + flag] = result.returncode
    obsolete = subprocess.run([build / "llama-server", "--no-mmap"], capture_output=True, timeout=10)
    checks["no_mmap_rejected"] = "invalid argument: --no-mmap" in obsolete.stderr.decode()
    with server("normal", profiles["normal"]) as number:
        checks["metrics_off"] = request(number, "/metrics")[0]
        checks["slots_get"] = request(number, "/slots")[0]
        checks["slots_erase"] = request(number, "/slots/0?action=erase", {})[0]
        checks["normal_generated"] = completion(number, dict(long_request, n_predict=8))["tokens_predicted"]
    with server("measurement", profiles["measurement"]) as number:
        status, metrics = request(number, "/metrics")
        checks["metrics_on"] = status
        (output / "metrics.txt").write_text(metrics)
        start = time.monotonic()
        control = completion(number, long_request)
        checks["control"] = dict(seconds=time.monotonic() - start, count=control["tokens_predicted"],
                                 stop=control.get("stop_type"), timings=control.get("timings"))
        reference = completion(number, dict(long_request, prompt=prefix + ",", n_predict=8))
        checks["prefix_reference"] = reference["content"]
        checks["cancel"] = []
        for _ in range(3):
            checks["cancel"].append(cancel(number, output / "measurement.log"))
        checks["prefix_match"] = all(item["next_content"] == reference["content"] for item in checks["cancel"])
    rpc_port = port()
    rpc_flags = ["-H", "127.0.0.1", "-p", str(rpc_port), "-t", "4"]
    results["rpc_flags"] = rpc_flags
    unsupported = subprocess.run([build / "ggml-rpc-server", "-m", "1024"], capture_output=True, timeout=10)
    checks["rpc_memory_flag_rejected"] = "unknown argument: -m" in unsupported.stderr.decode()
    with process("rpc", [build / "ggml-rpc-server", *rpc_flags]) as proc:
        ready(rpc_port, proc, False)
        checks["rpc_listening"] = True
        with server("rpc-client", base + ["--rpc", f"127.0.0.1:{rpc_port}", "-ngl", "99"]) as number:
            checks["rpc_generated"] = completion(number, dict(long_request, n_predict=8))["tokens_predicted"]

def terminate(_signum, _frame):
    raise SystemExit(124)

signal.signal(signal.SIGTERM, terminate)
try:
    run()
finally:
    (output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
