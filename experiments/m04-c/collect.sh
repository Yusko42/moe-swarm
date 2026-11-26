#!/usr/bin/env bash
# Run on the SSH client. Each phase uses two independent PAM sessions.
set -eu -o pipefail
umask 077
if [[ $# != 2 || ! $2 =~ ^(no|yes)$ || $1 == -* ]]; then
    echo "Использование: bash collect.sh qa@bench no|yes" >&2
    exit 64
fi
dest=$1
linger=$2
kit=$(cd -- "$(dirname -- "$0")" && pwd)
# Do not attach to a pre-existing SSH master. Its session may outlive the probe.
ssh_opts=(-T -o ControlPath=none -o ControlMaster=no -o ControlPersist=no)
pause=${M04C_LOGOUT_WAIT:-45}
[[ $pause =~ ^[1-9][0-9]*$ ]] || { echo 'M04C_LOGOUT_WAIT должен быть целым числом секунд > 0.' >&2; exit 64; }
image=${M04C_IMAGE:-ubuntu:26.04}
[[ $image == ubuntu:26.04 || $image =~ ^ubuntu@sha256:[a-f0-9]{64}$ ]] || { echo 'Укажите ubuntu:26.04 или ubuntu@sha256:<digest>.' >&2; exit 64; }
out=$(mktemp -d "$PWD/m04c-$linger-XXXXXXXX")
marker="${out##*/}-logout.scope"
ttl=$((10#$pause + 180))
archive() {
    tar -C "$(dirname "$out")" -czf "$out.tar.gz" "$(basename "$out")"
    echo "Данные: $out.tar.gz"
}
trap archive EXIT
(cd "$kit" && sha256sum collect.sh bench.sh probe.sh pressure.py) > "$out/scripts.sha256"
if [[ -f $kit/source.txt ]]; then
    cp "$kit/source.txt" "$out/source.txt"
elif git -C "$kit" rev-parse --verify HEAD > "$out/source.txt" 2>/dev/null; then
    git -C "$kit" status --short -- . >> "$out/source.txt"
fi

remote() {
    # bench.sh sees an ordinary remote non-root login, including its real bus.
    tar -C "$kit" -cf - bench.sh probe.sh pressure.py |
        ssh "${ssh_opts[@]}" "$dest" "set -eu
            umask 077
            d=\$(mktemp -d \"\$HOME/.m04c-XXXXXXXX\")
            trap 'rm -rf -- \"\$d\"' EXIT
            cd \"\$d\"
            tar -xf -
            chmod a+r probe.sh
            M04C_IMAGE=$image bash bench.sh $1 $linger $marker $ttl" 2>&1 | tee "$out/$1.log"
}

rc=0
remote before || rc=$?
before=$(sed -n 's/^manager_id=//p' "$out/before.log")
if [[ -z $before ]]; then
    echo "Не удалось подтвердить SSH-сессию и Linger=$linger. См. before.log." >&2
    exit 1
fi
echo "Все SSH-соединения скрипта закрыты. Пауза $pause с."
sleep "$pause"
remote after || rc=1
after=$(sed -n 's/^manager_id=//p' "$out/after.log")
lifetime=unexpected_or_unverified
if [[ -n $after ]]; then
    if [[ $linger == yes && $before == "$after" ]] || [[ $linger == no && $before != "$after" ]]; then
        lifetime=expected
    fi
fi
[[ $lifetime == expected ]] || rc=1
{
    echo "linger=$linger wait_seconds=$pause before=$before after=$after"
    echo "manager_lifetime=$lifetime"
    echo "collection_rc=$rc"
} | tee "$out/logout.txt"
exit "$rc"
