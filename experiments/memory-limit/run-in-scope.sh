#!/usr/bin/env bash
# Question 6, the "wrapper" variant: the same systemd-run call as in
# enter_limited_scope(), but outside the program.
#   ./run-in-scope.sh PROGRAM [ARG...]      (SWARM_MEMORY_MAX in bytes, default 8 GiB)
set -euo pipefail
exec systemd-run --user --scope --quiet --collect \
    --unit="swarm-wrap-$$.scope" \
    --property=MemoryMax="${SWARM_MEMORY_MAX:-8589934592}" \
    --property=MemorySwapMax=0 \
    --property=OOMPolicy=continue \
    -- "$@"
