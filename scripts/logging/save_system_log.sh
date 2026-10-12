#!/usr/bin/env bash
# Save a finite journal snapshot and complete syncfs before returning.
set -euo pipefail
out_root=${1:-/mnt/data_disk/logs}
sn=${2:-unknown_device}; sn=${sn,,}
max_bytes=${UGRIPPER_SYSTEM_LOG_MAX_BYTES:-104857600}
read_timeout=${UGRIPPER_SYSTEM_LOG_READ_TIMEOUT_S:-10}
[[ "$sn" =~ ^[a-z0-9_-]+$ && "$max_bytes" =~ ^[0-9]+$ && "$max_bytes" -ge 4 ]] || exit 2
[[ "$read_timeout" =~ ^[0-9]+([.][0-9]+)?$ && "$read_timeout" =~ [1-9] ]] || exit 2

# Capture the boundary before waiting for a concurrent snapshot.
# Optional --sync <epoch> <YYYYMMDD> pins the boundary for maintenance/tests.
cutoff=${4:-$(date +%s.%6N)}
day=${5:-$(date +%Y%m%d)}
[[ "$cutoff" =~ ^[0-9]+([.][0-9]+)?$ && "$day" =~ ^[0-9]{8}$ ]] || exit 2

# Serialize snapshots and wait for the previous writer. Returning while another
# task still writes to the disk would make Ready unsafe for physical removal.
exec 9>"${TMPDIR:-/tmp}/ugripper-system-log-${sn}.lock"
flock 9
mkdir -p -- "$out_root"
state="$out_root/.system_journal_cursor_${sn}"
dest="$out_root/system_${sn}_${day}.log"
tmp=$(mktemp "${TMPDIR:-/tmp}/ugripper-system-snapshot.XXXXXX")
trimmed=''; cursor_tmp=''
cleanup() {
    rm -f -- "$tmp"
    [[ -z "$trimmed" ]] || rm -f -- "$trimmed"
    [[ -z "$cursor_tmp" ]] || rm -f -- "$cursor_tmp"
}
trap cleanup EXIT
trap 'logger -t ugripper-system-log "snapshot failed: device=$sn stage=$stage" || true' ERR
stage=journal_read
cursor=''
[[ ! -r "$state" ]] || cursor=$(cat -- "$state")
read_journal() {
    timeout --kill-after=1s "${read_timeout}s" journalctl "$@" \
        --until="@$cutoff" --no-pager -o short-iso --show-cursor 2>/dev/null |
        tail -c "$max_bytes" >"$tmp"
}
if [[ -n "$cursor" ]]; then
    if read_journal --after-cursor="$cursor"; then :
    else
        rc=$?
        # A timeout is not an invalid cursor. Never commit a partial export.
        [[ "$rc" != 124 && "$rc" != 137 ]] || exit "$rc"
        # Journal rotation/reboot can remove the cursor. Recover current-boot
        # entries without scanning/deduplicating the existing disk log.
        read_journal -b 0
    fi
else
    read_journal -b 0
fi
# Bound temporary storage too; discard a possibly partial leading line.
snapshot_truncated=false
if (( $(stat -c %s -- "$tmp") == max_bytes )); then
    snapshot_truncated=true
    sed -i "1d" "$tmp"
fi
new_cursor=$(sed -n 's/^-- cursor: //p' "$tmp" | tail -n 1)
sed -i '/^-- cursor: /d' "$tmp"
old_size=0
[[ ! -f "$dest" ]] || old_size=$(stat -c %s -- "$dest")
new_size=$(stat -c %s -- "$tmp")
stage=publish
if (( old_size + new_size > max_bytes )) || [[ "$snapshot_truncated" == true ]]; then
    # Hysteresis avoids rewriting 100 MiB on every subsequent STOP. Only cap
    # crossings rewrite the log, retaining the latest ~75 MiB and whole lines.
    retain_bytes=$((max_bytes * 3 / 4))
    trimmed=$(mktemp "$out_root/.system-tail.XXXXXX")
    { [[ ! -f "$dest" ]] || cat -- "$dest"; cat -- "$tmp"; } |
        tail -c "$retain_bytes" | sed '1d' >"$trimmed"
    sync -f "$trimmed"
    mv -f -- "$trimmed" "$dest"
    trimmed=''
elif [[ ! -f "$dest" ]] && (( new_size > 0 )); then
    trimmed=$(mktemp "$out_root/.system-first.XXXXXX")
    cat -- "$tmp" >"$trimmed"
    sync -f "$trimmed"
    mv -f -- "$trimmed" "$dest"
    trimmed=''
elif (( new_size > 0 )); then
    # Normal snapshots append only the delta; no full-log read or deduplication.
    cat -- "$tmp" >>"$dest"
fi
# Preserve durability. Runtime must keep Writing until this syncfs completes.
stage=data_sync
[[ ! -f "$dest" ]] || sync -f "$dest"
stage=cursor_commit
if [[ -n "$new_cursor" ]]; then
    cursor_tmp=$(mktemp "$out_root/.system-cursor.XXXXXX")
    printf '%s\n' "$new_cursor" >"$cursor_tmp"
    sync -f "$cursor_tmp"
    mv -f -- "$cursor_tmp" "$state"
    cursor_tmp=''
fi
# Also persist renames/directory entries. A failed export or data flush never
# advances the cursor; retries may duplicate lines after an interrupted append.
sync -f "$out_root"
