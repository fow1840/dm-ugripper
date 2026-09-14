#!/usr/bin/env bash
# Save systemd journal entries incrementally to the data disk.
set -u
out_root=${1:-/mnt/data_disk/logs}; sn=${2:-unknown_device}; state="$out_root/.system_journal_cursor_${sn,,}"
mkdir -p "$out_root" 2>/dev/null || exit 1
cursor=''; [ -r "$state" ] && cursor=$(cat "$state" 2>/dev/null)
tmp=$(mktemp "$out_root/.system-log.XXXXXX") || exit 1
cursor_tmp=$(mktemp "$out_root/.system-cursor.XXXXXX") || { rm -f "$tmp"; exit 1; }
cleanup(){ rm -f "$tmp" "$cursor_tmp"; }; trap cleanup EXIT
if [ -n "$cursor" ] && journalctl --after-cursor="$cursor" --no-pager -o short-iso --show-cursor >"$tmp" 2>/dev/null; then :
else
  # Cursor is lost after journal rotation/clearing/reboot: current journal is the recovery source.
  journalctl -b 0 --no-pager -o short-iso --show-cursor >"$tmp" 2>/dev/null || exit 1
fi
new_cursor=$(sed -n 's/^-- cursor: //p' "$tmp" | tail -n 1)
sed -i '/^-- cursor: /d' "$tmp"
[ -s "$tmp" ] || { [ -n "$new_cursor" ] && printf '%s\n' "$new_cursor" >"$cursor_tmp" && mv -f "$cursor_tmp" "$state"; exit 0; }
today=$(date +%Y%m%d); dest="$out_root/system_${sn,,}_${today}.log"; merged=$(mktemp "$out_root/.system-merged.XXXXXX") || exit 1
# Keep existing records and remove exact duplicate lines from recovery reads.
if [ -f "$dest" ]; then awk '!seen[$0]++' "$dest" "$tmp" >"$merged" || exit 1; else cp "$tmp" "$merged" || exit 1; fi
sync -f "$merged" 2>/dev/null || true
mv -f "$merged" "$dest" || exit 1
if [ -n "$new_cursor" ]; then printf '%s\n' "$new_cursor" >"$cursor_tmp" && mv -f "$cursor_tmp" "$state"; fi
