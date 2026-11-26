"""Bounded page-cache churn in the leaf scope of a temporary user slice."""
import os
import pathlib
import sys
import tempfile
import time


def main():
    group = next(line[3:] for line in pathlib.Path("/proc/self/cgroup").read_text().splitlines()
                 if line.startswith("0::"))
    leaf = pathlib.Path("/sys/fs/cgroup") / group.lstrip("/")
    if not leaf.as_posix().endswith("/" + sys.argv[1]):
        raise RuntimeError(f"Unexpected scope: {leaf}")
    print(f"leaf={leaf} ancestor={leaf.parent}", flush=True)
    for key, expected in (("memory.max", "134217728"), ("memory.high", "20971520"),
                          ("memory.swap.max", "0")):
        actual = (leaf / key).read_text().strip()
        print(f"{key}={actual}", flush=True)
        if actual != expected:
            raise RuntimeError("Unexpected cgroup limits or subgroup")

    chunk = bytes(256 * 1024)
    # Regular file on the bench disk. Re-reading the working set induces reclaim.
    with tempfile.TemporaryFile(dir=os.getcwd()) as scratch:
        for _ in range(192):
            scratch.write(chunk)
        scratch.flush()
        os.fsync(scratch.fileno())
        deadline = time.monotonic() + 35
        sample_at = 0
        while time.monotonic() < deadline:
            if time.monotonic() >= sample_at:
                for key in ("memory.events", "memory.pressure", "memory.current"):
                    print(key, (leaf / key).read_text().strip().replace("\n", " "), flush=True)
                sample_at = time.monotonic() + 2
            if not scratch.read(len(chunk)):
                scratch.seek(0)
    print("pressure_completed", flush=True)


if __name__ == "__main__":
    main()
