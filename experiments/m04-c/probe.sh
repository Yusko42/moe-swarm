#!/usr/bin/env bash
# Standalone payload for a user scope and for the container CI profile.
set -eu
export LC_ALL=C
limit=134217728
die() {
    echo "cgroup_unavailable: $*" >&2
    exit 78
}

case ${1:-} in
    scope)
        unit="m04c-${BASHPID}-$RANDOM.scope"
        if ! command -v systemd-run >/dev/null; then
            die "Отсутствует systemd-run. Установите systemd на хосте; в CI используйте внешний лимит контейнера."
        fi
        systemd-run --user --scope --collect --unit="$unit" \
            -p MemoryAccounting=yes -p MemoryMax=$limit -p MemorySwapMax=0 \
            -p OOMPolicy=continue bash "$0" verify "$unit" && exit 0
        die "Проверьте systemctl --user status, SSH-вход через PAM, XDG_RUNTIME_DIR, пользовательскую шину и делегирование memory в cgroup v2. Для работы после выхода включите loginctl enable-linger."
        ;;
    verify)
        group=$(awk -F: '$1 == "0" {print substr($0,4)}' /proc/self/cgroup)
        [[ $group == /* && -e /sys/fs/cgroup/cgroup.controllers ]] ||
            die "Нужна единая иерархия cgroup v2, смонтированная в /sys/fs/cgroup."
        echo "uid=$(id -u) cgroup=$group"
        if [[ -n ${2:-} && ${group##*/} != "$2" ]]; then
            die "Проверьте ControlGroup созданного scope. Ожидался $2, получен $group."
        fi
        dir="/sys/fs/cgroup${group%/}"
        for key in memory.max memory.swap.max; do
            [[ -r $dir/$key ]] || die "Проверьте делегирование контроллера memory и поддержку swap accounting ($key)."
            printf '%s=%s\n' "$key" "$(<"$dir/$key")"
        done
        [[ $(<"$dir/memory.max") == "$limit" && $(<"$dir/memory.swap.max") == 0 ]] ||
            die "Установите MemoryMax=128M и MemorySwapMax=0 или передайте Docker --memory=128m --memory-swap=128m."
        if [[ -n ${2:-} ]]; then
            [[ $(<"$dir/memory.oom.group") == 0 ]] || die "Проверьте OOMPolicy=continue у scope."
        fi
        child_group=$(bash -c 'cat /proc/self/cgroup')
        [[ $child_group == "$(cat /proc/self/cgroup)" ]] || die "Дочерний процесс должен наследовать cgroup."
        echo "child_cgroup=$child_group"
        echo "limits_verified"
        ;;
    *) exit 64 ;;
esac
