#!/usr/bin/env bash
# Question 1: cgroup v2, user systemd and its memory controller - without root.
# Prints the facts and exits with 1 if the limit cannot be set.
uid=$(id -u)
user_cg=/sys/fs/cgroup/user.slice/user-$uid.slice/user@$uid.service
echo "os:                 $(. /etc/os-release && echo "$PRETTY_NAME")"
echo "kernel:             $(uname -r)"
echo "systemd:            $(systemctl --version | head -1)"
echo "uid:                $uid"
echo "/sys/fs/cgroup:     $(stat -fc %T /sys/fs/cgroup)"
echo "own cgroup:         $(cat /proc/self/cgroup)"
echo "root controllers:   $(cat /sys/fs/cgroup/cgroup.controllers)"
echo "user manager:       $(systemctl --user is-system-running 2>&1)"
echo "its controllers:    $(cat "$user_cg/cgroup.controllers" 2>&1)"
echo "DefaultOOMPolicy:   $(systemctl --user show -p DefaultOOMPolicy --value 2>&1)"
echo "systemd-oomd:       $(systemctl is-active systemd-oomd 2>&1)"
echo "MemTotal/Available: $(awk '/^MemTotal|^MemAvailable/ {printf "%s %s kB  ", $1, $2}' /proc/meminfo)"
echo "SwapTotal:          $(awk '/^SwapTotal/ {print $2 " kB"}' /proc/meminfo)"

status=0
[ "$uid" != 0 ] || { echo "FAIL: run as a regular user, not root"; status=1; }
[ "$(stat -fc %T /sys/fs/cgroup)" = cgroup2fs ] || { echo "FAIL: /sys/fs/cgroup is not cgroup v2"; status=1; }
grep -qw memory /sys/fs/cgroup/cgroup.controllers || { echo "FAIL: no memory controller"; status=1; }
systemctl --user is-system-running >/dev/null 2>&1 || [ "$(systemctl --user is-system-running 2>/dev/null)" = degraded ] \
    || { echo "FAIL: user systemd is not running"; status=1; }
grep -qw memory "$user_cg/cgroup.controllers" 2>/dev/null || { echo "FAIL: memory controller is not delegated to user systemd"; status=1; }
[ "$status" = 0 ] && echo "OK: a regular user can create a group with MemoryMax"
exit "$status"
