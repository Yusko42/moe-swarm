#!/usr/bin/env python3
"""Core memory/lifecycle experiments. Run: ./build.sh && python3 run_scenarios.py.
Each case: start -> verify limits -> act -> measure -> stop -> check cleanup.
The 8 GiB case checks configuration; memory pressure is tested at 1 GiB.
"""
import hashlib
import json
import os
import pty
import select
import signal
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
GIB = 1 << 30
ROLES = ("parent", "child-0", "child-1")
# name, action, PDEATHSIG, limit. Only oom-continue allocates 2 x 640 MiB.
CASES = [
    ("probe-8g", "exit", True, 8 * GIB),
    ("oom-continue", "alloc", True, GIB),
    ("scope-exit", "exit", False, GIB),
    ("scope-ctrl-c", "ctrl-c", False, GIB),
    ("scope-kill9", "kill9", False, GIB),
    ("scope-stop", "stop", False, GIB),
    ("pdeath-exit", "exit", True, GIB),
    ("pdeath-ctrl-c", "ctrl-c", True, GIB),
    ("pdeath-kill9", "kill9", True, GIB),
    ("selfexec-kill9", "kill9", True, 8 * GIB),
    ("wrapper-ctrl-c", "ctrl-c", True, 8 * GIB),
]


def save(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def load(path):
    return json.loads(path.read_text()) if path.exists() else None


def command(argv):
    try:
        done = subprocess.run(argv, capture_output=True, text=True, timeout=20)
        return {"code": done.returncode, "out": (done.stdout + done.stderr).strip()}
    except (OSError, subprocess.TimeoutExpired) as error:
        return {"code": None, "out": str(error)}


def unit_state(unit):
    return command(["systemctl", "--user", "show", unit, "-p", "LoadState", "--value"])["out"]


def state(record):
    """PID plus start time identifies our process, even after PID reuse."""
    try:
        fields = Path(f"/proc/{record['pid']}/stat").read_text().rsplit(")", 1)[1].split()
        return fields[0] if int(fields[19]) == record["starttime"] else "gone"
    except FileNotFoundError:
        return "gone"


def alive(record):
    return state(record) not in ("gone", "Z", "X")


def wait_until(condition, seconds, tick=lambda: None):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        tick()
        if condition():
            return True
        time.sleep(0.02)
    return False


class Terminal:
    """All cases get a real terminal: sending byte 0x03 exercises Ctrl+C."""
    def __init__(self, argv, log):
        self.log = log.open("wb")
        self.code = None
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            try:
                os.execvp(argv[0], argv)
            finally:
                os._exit(127)

    def pump(self):
        while select.select([self.fd], [], [], 0)[0]:
            try:
                data = os.read(self.fd, 65536)
            except OSError:  # Linux PTY returns EIO after the last slave closes.
                break
            if not data:
                break
            self.log.write(data)
            self.log.flush()

    def poll(self):
        if self.code is None:
            pid, status = os.waitpid(self.pid, os.WNOHANG)
            if pid:
                self.code = os.waitstatus_to_exitcode(status)
        return self.code

    def close(self):
        clean = wait_until(lambda: self.poll() is not None, 5, self.pump)
        if not clean:
            os.kill(self.pid, signal.SIGKILL)
            self.code = os.waitstatus_to_exitcode(os.waitpid(self.pid, 0)[1])
        self.pump()
        os.close(self.fd)
        self.log.close()
        return clean


def snapshot(group):
    """Missing files are missing data, never a zero memory reading."""
    try:
        result = {key: int((group / ("memory." + key)).read_text())
                  for key in ("max", "current", "peak", "swap.max", "swap.current", "swap.peak")}
        result["events"] = dict((key, int(value)) for key, value in
                                (line.split() for line in (group / "memory.events").read_text().splitlines()))
        return result
    except FileNotFoundError:
        return None  # --collect can remove the group as soon as it becomes empty.


def records_in(directory, demo):
    names = ("inside", "child") if demo else ROLES
    return {name: record for name in names if (record := load(directory / f"{name}.json"))}


def run_case(root, spec):
    name, action, bound, limit = spec
    demo = name.startswith(("selfexec-", "wrapper-"))
    directory = root / name
    directory.mkdir()
    unit = None if demo else f"moe13-compact-{os.getpid()}-{name}.scope"
    argv = [str(HERE / "build" / "swarm_demo"), "--out", str(directory)] if demo else [
        "systemd-run", "--user", "--scope", "--quiet", "--collect", f"--unit={unit}",
        f"--property=MemoryMax={limit}", "--property=MemorySwapMax=0",
        "--property=OOMPolicy=continue", "--property=TimeoutStopSec=5s", "--",
        str(HERE / "build" / "workload"), "parent", "--out", str(directory),
        "--mib", "640" if action == "alloc" else "0", "--lifetime", "45"]
    if not demo and bound:
        argv.append("--pdeathsig")
    if name.startswith("wrapper-"):
        argv.insert(0, str(HERE / "run-in-scope.sh"))
    save(directory / "command.json", argv)
    result = {"name": name, "limit_bytes": limit, "oom_policy": "continue", "checks": {}}
    checks = result["checks"]
    terminal, group, records, samples = None, None, {}, []

    def tick():
        terminal.pump()
        if group is not None and (sample := snapshot(group)) is not None:
            samples.append(sample)

    def collect_records():
        nonlocal unit, group
        records.update(records_in(directory, demo))
        parent = records.get("inside" if demo else "parent")
        if parent:
            group = Path(parent["memory"]["dir"])
            unit = group.name
        elif demo and terminal:
            try:
                membership = Path(f"/proc/{terminal.pid}/cgroup").read_text().strip().split("/")[-1]
                if membership.startswith(("swarm-demo-", "swarm-wrap-")) and membership.endswith(".scope"):
                    unit = membership
            except FileNotFoundError:
                pass

    try:
        terminal = Terminal(argv, directory / "terminal.log")
        ready = directory / "ready.json"
        wait_until(lambda: ready.exists() or terminal.poll() is not None, 10, tick)
        if not ready.exists():
            raise RuntimeError("startup failed or timed out; see terminal.log")
        records = records_in(directory, demo)
        parent = records["inside" if demo else "parent"]
        group = Path(parent["memory"]["dir"])
        if demo:
            unit = parent["unit"]
            before = load(directory / "before.json")
            checks["entry_method"] = (before["unit"] == unit if name.startswith("wrapper-") else before["unit"] != unit)
            same_group = records["child"]["cgroup"] == str(group)
            memories = [parent["memory"]]
        else:
            same_group = all(r["memory"]["dir"] == str(group) for r in records.values())
            memories = [r["memory"] for r in records.values()]
        checks["started_inside_limit"] = (len(records) == (2 if demo else 3) and group.name == unit and same_group
                                          and all(m["max"] == limit and m["swap_max"] == 0 for m in memories))
        if not checks["started_inside_limit"]:
            raise RuntimeError("expected limits are absent; allocation was not started")
        tick()
        if not demo:
            (directory / "go").touch()
        if action == "alloc":
            def settled():
                exits = load(directory / "child-exits.json") or []
                allocated = sum((directory / f"child-{i}-alloc.json").exists() for i in (0, 1))
                return len(exits) + allocated == 2 or not alive(parent)
            checks["allocation_finished"] = wait_until(settled, 30, tick)
        elif action == "ctrl-c":
            os.write(terminal.fd, b"\x03")
        elif action in ("exit", "kill9"):
            os.kill(parent["pid"], signal.SIGUSR1 if action == "exit" else signal.SIGKILL)
        elif action == "stop":
            checks["explicit_stop"] = command(["systemctl", "--user", "stop", unit])["code"] == 0
        wait_until(lambda: False, 1.5, tick)
        result["after_action"] = {role: state(r) for role, r in records.items()}
        children = [r for role, r in records.items() if role != ("inside" if demo else "parent")]
        checks["parent_after_action"] = alive(parent) == (action == "alloc")
        expected_children = 1 if action == "alloc" else (0 if bound or action == "stop" else 2)
        checks["children_after_action"] = sum(alive(r) for r in children) == expected_children
        if action == "alloc":
            result["child_exits"] = load(directory / "child-exits.json") or []
            checks["oom_victim_recorded"] = any(e["status"] == "signal KILL" and e["oom_kill_delta"] > 0
                                                 for e in result["child_exits"])
            allocated = [load(directory / f"child-{i}-alloc.json") for i in (0, 1)]
            checks["survivor_allocated_640m"] = any(a and a["bytes"] == a["target"] == 640 << 20 for a in allocated)
    except (Exception, KeyboardInterrupt) as error:
        result["error"] = repr(error)
    finally:
        collect_records()
        result["stop"] = command(["systemctl", "--user", "stop", unit]) if unit else None
        checks["launcher_reaped"] = terminal.close() if terminal else False
        # A slow startup can register its scope/children after the first stop.
        # Re-read their identities and stop again once the launcher is reaped.
        collect_records()
        result["unit"] = unit
        result["stop_after_reap"] = command(["systemctl", "--user", "stop", unit]) if unit else None
        wait_until(lambda: all(state(r) == "gone" for r in records.values()), 5)
        result["records"] = records
        result["cleanup"] = {role: state(r) for role, r in records.items()}
        checks["nothing_left_after_cleanup"] = bool(records) and all(s == "gone" for s in result["cleanup"].values())
        checks["unit_removed"] = unit is not None and unit_state(unit) == "not-found"
        checks["group_removed"] = group is not None and not group.exists()
        # Emergency cleanup is reported as a failure, even if it succeeds.
        for record in records.values():
            if alive(record):
                try:
                    os.kill(record["pid"], signal.SIGKILL)
                except ProcessLookupError:
                    pass
        wait_until(lambda: all(state(r) == "gone" for r in records.values()), 5)
        result["exit_code"] = terminal.code if terminal else None
    save(directory / "metrics.json", samples)
    observed = {"peak": max((s["peak"] for s in samples), default=None),
                "current_max_sample": max((s["current"] for s in samples), default=None),
                "swap_max_seen": max((max(s["swap.current"], s["swap.peak"]) for s in samples), default=None),
                "events": {key: max(s["events"].get(key, 0) for s in samples)
                           for key in {key for s in samples for key in s["events"]}}}
    result["observed"] = observed
    checks["memory_recorded"] = bool(samples) and all(s["max"] == limit and s["swap.max"] == 0 for s in samples)
    checks["swap_not_used"] = observed["swap_max_seen"] == 0
    checks["oom_events"] = (all(observed["events"].get(key, 0) > 0 for key in ("max", "oom", "oom_kill"))
                             if action == "alloc" else observed["events"].get("oom_kill", 0) == 0)
    if action == "exit":
        checks["exit_status"] = result["exit_code"] == 0
    elif action in ("ctrl-c", "kill9"):
        checks["exit_status"] = result["exit_code"] == (130 if demo and action == "ctrl-c" else
                                                       -signal.SIGINT if action == "ctrl-c" else -signal.SIGKILL)
    if demo and action == "ctrl-c":
        checks["rpc_stopped_by_parent"] = (load(directory / "exit.json") or {}).get("child") == "signal TERM"
    return result


def outside_status(process):
    if process.poll() is not None:
        return {"alive": False}
    fields = dict(line.split(":", 1) for line in Path(f"/proc/{process.pid}/status").read_text().splitlines())
    return {"alive": True, "rss": int(fields["VmRSS"].split()[0]) * 1024,
            "swap": int(fields["VmSwap"].split()[0]) * 1024}


def main():
    if sys.platform != "linux" or os.geteuid() == 0:
        sys.exit("Run in Linux/WSL as a regular user, without sudo")
    if any(not (HERE / "build" / name).exists() for name in ("workload", "swarm_demo")):
        sys.exit("Run ./build.sh first")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    root = HERE / "results" / f"compact-{stamp}"
    root.mkdir(parents=True)
    meminfo = dict(line.split(":", 1) for line in Path("/proc/meminfo").read_text().splitlines())
    environment = {"check_env": command(["bash", str(HERE / "check-env.sh")]), "uname": list(os.uname()),
                   "os_release": Path("/etc/os-release").read_text(), "swaps": Path("/proc/swaps").read_text(),
                   "MemAvailable": int(meminfo["MemAvailable"].split()[0]) * 1024,
                   "SwapTotal": int(meminfo["SwapTotal"].split()[0]) * 1024,
                   "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    save(root / "environment.json", environment)
    if environment["check_env"]["code"] != 0 or environment["MemAvailable"] < 4 * GIB:
        save(root / "summary.json", {"status": "BLOCKED", "reason": "preflight failed or less than 4 GiB available",
                                     "environment": environment})
        sys.exit(f"BLOCKED: see {root / 'environment.json'}")
    # A process outside the tested scopes holds 256 MiB throughout all cases.
    code = "import mmap,time\nm=mmap.mmap(-1,256<<20)\nfor i in range(0,len(m),4096): m[i]=1\nprint('ready',flush=True)\ntime.sleep(3600)"
    sentinel = subprocess.Popen([sys.executable, "-c", code], stdout=subprocess.PIPE)
    results = []
    try:
        if not select.select([sentinel.stdout], [], [], 10)[0] or sentinel.stdout.readline() != b"ready\n":
            raise RuntimeError("outside process did not start")
        initial = outside_status(sentinel)
        environment["outside_cgroup"] = Path(f"/proc/{sentinel.pid}/cgroup").read_text().strip()
        for spec in CASES:
            result = run_case(root, spec)
            outside = outside_status(sentinel)
            result["outside_process"] = outside
            result["checks"]["outside_process_unaffected"] = (outside["alive"] and outside["swap"] == 0
                                                               and abs(outside["rss"] - initial["rss"]) < 4 << 20)
            result["status"] = "PASS" if "error" not in result and all(result["checks"].values()) else "FAIL"
            results.append(result)
            save(root / result["name"] / "result.json", result)
            failed = [key for key, passed in result["checks"].items() if not passed]
            print(f"{result['name']:24} {result['status']} {' '.join(failed)} {result.get('error', '')}", flush=True)
            if result["status"] == "FAIL":
                break
    finally:
        if sentinel.poll() is None:
            sentinel.terminate()
        try:
            sentinel.wait(timeout=5)
        except subprocess.TimeoutExpired:
            sentinel.kill()
            sentinel.wait()
        sentinel.stdout.close()
        audit = {"processes_left": [r for result in results for r in result["records"].values() if state(r) != "gone"],
                 "units_left": [r["unit"] for r in results if r["unit"] and unit_state(r["unit"]) != "not-found"],
                 "sentinel_exit": sentinel.returncode}
        save(root / "final-audit.json", audit)
        save(root / "summary.json", {"environment": environment, "results": results, "final_audit": audit,
                                     "swap_verification": "available" if environment["SwapTotal"] else
                                     "BLOCKED: SwapTotal=0; swap.max=0 is checked, suppression cannot be tested"})
    print(f"PASS {sum(r['status'] == 'PASS' for r in results)}/{len(CASES)}; results: {root}")
    return 0 if len(results) == len(CASES) and all(r["status"] == "PASS" for r in results) and not (
        audit["processes_left"] or audit["units_left"]) else 1


if __name__ == "__main__":
    sys.exit(main())
