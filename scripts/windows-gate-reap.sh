#!/usr/bin/env bash
# Stop whatever an aborted Windows gate run left running (UTA-0222).
#
# Runs ON the Windows test machine, in Git Bash; scripts/ci-matrix.sh copies it
# there with each run and calls it before touching the checkout. Measured
# 2026-09-28: when the ssh connection drops, the remote script does not end --
# a Git Bash shell may linger, and native tools (MSBuild, cl, ctest) keep
# running after their Git Bash parent is gone. Neither taskkill nor the WMI
# process table is allowed to this account across ssh logons ("Access
# denied"); Git Bash's `kill -f -W` is.
#
# So it stops, by name, every build tool the gate starts and every other Git
# Bash shell of this account, sparing its own chain of parents. That is safe
# only because the account exists for the gate alone and the caller holds the
# local gate lock, so no other gate run exists to be hurt.
set -uo pipefail

# This script's own Git Bash ancestry, which includes the run calling it.
spared=" "
pid=$$
while [[ $pid =~ ^[0-9]+$ && $pid -gt 1 ]]; do
    spared+="$pid "
    pid=$(cat "/proc/$pid/ppid" 2>/dev/null || echo 1)
done

stopped=0
# ps -W: PID PPID PGID WINPID TTY UID STIME COMMAND. STIME can be two words
# and the command can hold spaces, so the command is found by where it
# starts: a / or a drive letter. Build tools go by image name, other shells
# by their Git Bash pid.
while read -r msys winpid command; do
    if [[ $command =~ /usr/bin/(bash|sh)$ ]]; then
        [[ $spared == *" $msys "* ]] && continue
        /usr/bin/kill -f "$msys" 2>/dev/null && stopped=$((stopped + 1))
    elif [[ ${command,,} =~ (^|[\\/])(msbuild|cl|link|ctest|cmake|glslc|ccache|uta_[a-z0-9_]+)\.exe$ ]]; then
        /usr/bin/kill -f -W "$winpid" 2>/dev/null && stopped=$((stopped + 1))
    fi
done < <(ps -W | awk 'NR > 1 && match($0, / (\/|[A-Za-z]:\\)/) { print $1, $4, substr($0, RSTART + 1) }')

if [[ $stopped -gt 0 ]]; then
    printf 'reap: stopped %d process(es) an aborted gate run left running\n' "$stopped"
fi
exit 0
