#!/usr/bin/env bash
# Executed in a freshly unpacked kit over an SSH connection.
set -u
export LC_ALL=C SYSTEMD_PAGER=cat
cd -- "$(dirname -- "$0")" || exit 1
stage=$1
expected=$2

marker=$3
ttl=$4

# Keep an actual user scope alive across SSH disconnect and bound its lifetime
# even if the client never reconnects.
start_marker() {
    nohup systemd-run --user --scope --collect --unit="$marker" \
        -p MemoryMax=128M -p MemorySwapMax=0 -p OOMPolicy=continue \
        -p RuntimeMaxSec="${ttl}s" sleep "$ttl" </dev/null >marker.log 2>&1 &
    local attempt
    for attempt in {1..30}; do
        if systemctl --user is-active --quiet "$marker"; then
            systemctl --user show "$marker" -p Id -p ActiveState -p ControlGroup
            return 0
        fi
        sleep 0.1
    done
    cat marker.log
    return 1
}

check_marker() {
    local units state
    units=$(systemctl --user list-units --all --plain --full --no-legend "$marker") || return 1
    state=$(awk -v unit="$marker" '$1 == unit {print $3}' <<< "$units")
    state=${state:-absent}
    echo "unit=$marker state=$state expected_linger=$expected"
    if [[ $state == active ]]; then
        systemctl --user stop "$marker" || return 1
        [[ $expected == yes ]]
    else
        [[ ( $state == absent || $state == inactive ) && $expected == no ]]
    fi
}

case_run() {
    local name=$1 rc
    shift
    echo "BEGIN $name $(date -u -Is)"
    "$@"
    rc=$?
    echo "END $name rc=$rc"
    return "$rc"
}

session() {
    local current manager
    id
    echo "ssh=${SSH_CONNECTION:+yes} xdg_runtime=${XDG_RUNTIME_DIR:-unset} dbus=${DBUS_SESSION_BUS_ADDRESS:+set}"
    loginctl show-user "$(id -u)" -p Linger -p Sessions -p State
    current=$(loginctl show-user "$(id -u)" -p Linger --value) || return 1
    [[ $current == "$expected" ]] || { echo "Ожидался Linger=$expected. Проверьте настройки тестового пользователя."; return 1; }
    [[ -n ${SSH_CONNECTION:-} && $(id -u) != 0 ]] || { echo "Нужна SSH-сессия обычного пользователя."; return 1; }
    manager=$(systemctl show "user@$(id -u).service" -p InvocationID --value) || return 1
    [[ $manager =~ ^[0-9a-f]{32}$ && $manager != 00000000000000000000000000000000 ]] || return 1
    echo "manager_id=$manager"
}

versions() {
    cat /etc/os-release
    uname -srmo
    cat /proc/swaps
    local tool
    for tool in systemd systemd-run oomctl docker git gcc g++ clang clang-format clang-tidy cmake ninja conan ccache python3; do
        if command -v "$tool" >/dev/null; then
            "$tool" --version 2>&1 | head -n 1
        else
            echo "$tool=MISSING"
        fi
    done
    dpkg-query -W -f='${binary:Package}=${Version}\n' | awk -F= '$1 ~ /^(systemd|libpam-systemd|libsystemd|gcc|g\+\+|clang|cmake|ninja-build|ccache|docker|containerd|python3)([-:.0-9]|$)/'
}

negative() {
    local rc=0
    "$@" || rc=$?
    echo "negative_probe_rc=$rc"
    [[ $rc == 78 ]]
}

container() {
    local image=${M04C_IMAGE:-ubuntu:26.04} image_id
    docker version || return 1
    docker image inspect "$image" >/dev/null 2>&1 || docker pull "$image" || return 1
    docker image inspect --format 'image={{.Id}} repo_digests={{json .RepoDigests}} os={{.Os}} arch={{.Architecture}}' "$image" || return 1
    image_id=$(docker image inspect --format '{{.Id}}' "$image") || return 1
    docker run --rm --network=none --cgroupns=private --memory=128m --memory-swap=128m \
        --user "$(id -u):$(id -g)" --mount "type=bind,src=$PWD/probe.sh,dst=/probe.sh,readonly" \
        "$image_id" bash -c '
            cat /etc/os-release
            echo "pid1=$(cat /proc/1/comm)"
            bash /probe.sh scope
            rc=$?
            echo "container_scope_rc=$rc"
            bash /probe.sh verify && [[ $rc == 78 ]]
        '
}

oom_context() {
    local unit
    systemctl is-active systemd-oomd.service
    cat /proc/pressure/memory
    for unit in -.slice user.slice "user-$(id -u).slice" "user@$(id -u).service"; do
        systemctl show -p ControlGroup -p ManagedOOMMemoryPressure -p ManagedOOMMemoryPressureLimit -- "$unit"
    done
    systemctl --user show app.slice -p ControlGroup -p ManagedOOMMemoryPressure
    timeout 5 oomctl dump
}

pressure() (
    local mode=$1 name="m04c${1}${BASHPID}${RANDOM}" since pid rc
    local slice="$name.slice" unit="$name.scope"
    local workload="$PWD/$mode.txt" journal="$PWD/$mode-journal.txt"
    # oomd monitors the slice and can select its leaf scope. Both exist only for this trial.
    cat > "$slice" <<UNIT
[Slice]
ManagedOOMMemoryPressure=$mode
ManagedOOMMemoryPressureLimit=5%
ManagedOOMMemoryPressureDurationSec=5s
UNIT
    systemctl --user link --runtime "$PWD/$slice" || return 1
    trap 'systemctl --user stop "$slice"; systemctl --user disable --runtime "$slice"' EXIT
    since=$(date --iso-8601=seconds)
    echo "unit=$unit monitored_slice=$slice monitor=$mode"
    systemd-run --user --scope --slice="$slice" --unit="$unit" --collect \
        -p MemoryHigh=20M -p MemoryMax=128M -p MemorySwapMax=0 -p OOMPolicy=continue \
        -p RuntimeMaxSec=60s python3 -u "$PWD/pressure.py" "$slice/$unit" >"$workload" 2>&1 &
    pid=$!
    sleep 2
    systemctl --user show "$slice" -p ControlGroup -p ManagedOOMMemoryPressure -p ManagedOOMMemoryPressureLimit -p ManagedOOMMemoryPressureDurationUSec
    timeout 5 oomctl dump
    wait "$pid"
    rc=$?
    cat "$workload"
    echo "workload_rc=$rc"
    systemctl is-active systemd-oomd.service
    journalctl --no-pager -u systemd-oomd.service --since="$since" >"$journal"
    cat "$journal"
    journalctl --user --no-pager -u "$unit" --since="$since"
    if grep -qx pressure_completed "$workload" && ((rc == 0)); then
        echo "pressure_result=completed"
        return 0
    fi
    if grep -F "$slice/$unit" "$journal" | grep -qi 'killed'; then
        echo "pressure_result=oomd_kill"
        return 0
    fi
    echo "pressure_result=inconclusive"
    return 1
)

if ! case_run session session; then
    case_run ssh-scope bash probe.sh scope
    exit 2
fi
if [[ $stage == after ]]; then
    case_run logout-scope check_marker
    exit $?
fi
case_run versions versions
case_run ssh-scope bash probe.sh scope
scope_rc=$?
case_run invalid-bus negative env DBUS_SESSION_BUS_ADDRESS=unix:path=/dev/null/m04c XDG_RUNTIME_DIR=/dev/null/m04c bash probe.sh scope
negative_rc=$?
oomd_rc=0
if [[ $expected == no ]]; then
    case_run docker container
    docker_rc=$?
    case_run oomd-baseline oom_context || oomd_rc=1
    systemctl is-active --quiet systemd-oomd.service || oomd_rc=1
    for mode in auto kill; do
        case_run "oomd-$mode" pressure "$mode" || oomd_rc=1
    done
else
    docker_rc=0
fi
case_run logout-scope-start start_marker || exit 1
((scope_rc == 0 && negative_rc == 0 && docker_rc == 0 && oomd_rc == 0))
