#!/usr/bin/env bash
# Builds the test programs into build/ (g++ from Ubuntu 26.04, no other dependencies).
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
for program in workload swarm_demo; do
    g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread -o "build/$program" "src/$program.cpp"
done
echo "built: build/workload build/swarm_demo"
