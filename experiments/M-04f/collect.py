#!/usr/bin/env python3
import hashlib
import json
import pathlib
import re
import subprocess
import sys

root = pathlib.Path(sys.argv[1]).resolve()
out = pathlib.Path(__file__).resolve().parent
build = root / "build"
source = out.parents[1] / "external" / "llama"

def command(*args):
    return subprocess.check_output(args, text=True).strip()

def artifact(path):
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    return {"name": path.name, "bytes": path.stat().st_size, "sha256": digest}

bins = [artifact(build / "bin" / name) for name in ("llama-server", "ggml-rpc-server")]
(out / "SHA256SUMS").write_text("".join(f"{item['sha256']}  {item['name']}\n" for item in bins))
model = artifact(root / "Qwen3-0.6B-Q3_K_M.gguf")
model.update(source="https://huggingface.co/unsloth/Qwen3-0.6B-GGUF/resolve/50968a4468ef4233ed78cd7c3de230dd1d61a56b/Qwen3-0.6B-Q3_K_M.gguf")
assert model["sha256"] == "30bb9f9222cf58c076277879881a24e7127a10be32076d76d8c70f8defc01f5d"
flag_pattern = r"^((?:CMAKE_BUILD_TYPE|BUILD_SHARED_LIBS|LLAMA_OPENSSL|LLAMA_BUILD_(?:TESTS|EXAMPLES|NUMBER)|GGML_[A-Z0-9_]+):[^=]+)=(.*)$"
cache = dict(re.findall(flag_pattern, (build / "CMakeCache.txt").read_text(), re.MULTILINE))
observations = json.loads((root / "check/results.json").read_text())
checks = observations["checks"]
cancel = checks.get("cancel", [])
confirmed = (
    len(cancel) == 3
    and checks["control"]["count"] == 128
    and all(c["processing_after_close"][-1][1] == 0 and c["log"] for c in cancel)
    and checks.get("prefix_match") is True
)
manifest = {
    "experiment": "M-04f",
    "llama_commit": command("git", "-C", source, "rev-parse", "HEAD"),
    "llama_clean": not command("git", "-C", source, "status", "--porcelain"),
    "os": command("lsb_release", "-ds"),
    "architecture": command("uname", "-m"),
    "kernel": command("uname", "-r"),
    "tools": {name: command(name, "--version").splitlines()[0]
              for name in ("g++", "cmake", "ninja", "git", "python3", "curl", "ld")},
    "build_flags": re.findall(r"-D[^\s]+", (out / "build.sh").read_text()),
    "cmake_cache": cache,
    "binary_dir": str(build / "bin"),
    "strip": False,
    "elf_isa": {item["name"]: next(line.strip() for line in
        command("readelf", "-n", build / "bin" / item["name"]).splitlines()
        if "ISA needed" in line) for item in bins},
    "runtime_packages": {name: command("dpkg-query", "-W", "-f=${Version}", name)
                         for name in ("libc6", "libgomp1", "libstdc++6")},
    "binaries": bins,
    "dynamic_dependencies": {item["name"]: [
        row.strip().split()[0] for row in command("ldd", build / "bin" / item["name"]).splitlines()
    ] for item in bins},
    "model": model,
    "observations": observations,
    "decision": {
        "disconnect_confirmed_on_stand": confirmed,
        "slots": "disabled",
        "metrics": "measurement only",
        "bind": "127.0.0.1",
    },
    "limits": ["single physical host", "LAN untested", "Qwen3-30B-A3B untested",
               "partial E-03g coverage", "no byte-for-byte rebuild comparison",
               "no cross-host CPU compatibility test"],
}
(out / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
for path in (root / "check").iterdir():
    if path.suffix == ".txt":
        (out / path.name).write_text("\n".join(line.rstrip() for line in path.read_text().splitlines()).rstrip() + "\n")
patterns = re.compile(r"cancel|release|offload|RPC|listen|all slots are idle|memory breakdown|local cache|transport|accepted client connection", re.I)
with (out / "runtime.log").open("w") as log:
    for path in sorted((root / "check").glob("*.log")):
        log.write(f"\n[{path.name}]\n")
        for line in path.read_text().splitlines():
            if patterns.search(line):
                log.write(line + "\n")
