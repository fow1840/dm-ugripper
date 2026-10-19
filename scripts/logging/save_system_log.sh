#!/usr/bin/env bash
# New snapshots accept the mounted disk root, never a legacy logs directory.
set -euo pipefail
exec /usr/bin/python3 "$(dirname "$(readlink -f "$0")")/system_log_snapshot.py" "${1:-/mnt/data_disk}"
