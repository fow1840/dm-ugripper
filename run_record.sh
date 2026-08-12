#!/bin/bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$script_dir"
shell_common="$project_root/scripts/lib/ugripper_shell_common.sh"

if [ ! -f "$shell_common" ]; then
    printf '[%s][tid %s][E][run_record %s] [run-record] missing shell helper: %s error_code=UGR-70-01\n' \
        "$(date '+%s.%6N')" "$BASHPID" "$LINENO" "$shell_common" >&2
    exit 1
fi

# shellcheck source=/dev/null
. "$shell_common"

log_info() {
    ugripper_log "run-record" "INFO" "$*"
}

log_error() {
    local error_code="${1:-UGR-00-01}"
    shift || true
    ugripper_log "run-record" "ERROR" "$error_code" "$*"
}

DEFAULT_RUNTIME_BIN="$project_root/bin/UgripperRuntime/UgripperRuntime"
RUNTIME_BIN="${RUNTIME_BIN_OVERRIDE:-$DEFAULT_RUNTIME_BIN}"
ENV_FILE="${ENV_FILE_OVERRIDE:-/etc/environment}"
DISK_ROOT="${DISK_ROOT_OVERRIDE:-/mnt/data_disk}"
LOG_DIR_LOCAL="${LOG_DIR_LOCAL_OVERRIDE:-/tmp}"
LOG_DIR_DISK="$DISK_ROOT/logs"

# Keep Rockchip MPP encoder chatter out of journald during normal recording.
export mpp_debug="${mpp_debug:-0}"
export mpp_log_level="${mpp_log_level:-2}"
export mpp_syslog_perror="${mpp_syslog_perror:-0}"

DEVICE_SN="$(ugripper_read_env_value "$ENV_FILE" "DEVICE_SN" || true)"
DEVICE_SN_LOWER="${DEVICE_SN,,}"
DEVICE_SN_LOWER="${DEVICE_SN_LOWER:-unknown_device}"
TODAY="$(date +%Y%m%d)"

LOG_FILE_LOCAL="$LOG_DIR_LOCAL/umi_sys_${DEVICE_SN_LOWER}_${TODAY}.log"
LOG_FILE_DISK="$LOG_DIR_DISK/umi_sys_${DEVICE_SN_LOWER}_${TODAY}.log"
LOG_SYNC_POS_FILE="/tmp/umi_sys_${DEVICE_SN_LOWER}_${TODAY}.pos"
LOG_SYNC_LOCK_FILE="/tmp/umi_sys_${DEVICE_SN_LOWER}_${TODAY}.lock"
LOG_WRITER="$project_root/scripts/logging/ugripper_log_writer.py"

cleanup_old_logs() {
    local dir=""
    local file=""

    for dir in "$LOG_DIR_LOCAL" "$LOG_DIR_DISK"; do
        [ -d "$dir" ] || continue

        shopt -s nullglob
        for file in "$dir"/umi_sys_"${DEVICE_SN_LOWER}"_*.log; do
            if [[ "$file" != *"${TODAY}.log" ]]; then
                log_info "deleting old log: $file"
                rm -f "$file"
            fi
        done
        shopt -u nullglob
    done
}

if [ ! -x "$RUNTIME_BIN" ]; then
    log_error "UGR-70-01" "record_runtime binary not found: $RUNTIME_BIN"
    log_info "expected runtime path: $DEFAULT_RUNTIME_BIN"
    exit 1
fi

is_storage_ready() {
    local mount_source mount_opts

    if ! findmnt -rn --target "$DISK_ROOT" >/dev/null 2>&1; then
        return 1
    fi

    mount_source="$(findmnt -rn -o SOURCE --target "$DISK_ROOT" 2>/dev/null || true)"
    mount_opts="$(findmnt -rn -o OPTIONS --target "$DISK_ROOT" 2>/dev/null || true)"

    [ -n "$mount_source" ] || return 1
    [ -e "$mount_source" ] || return 1

    case ",$mount_opts," in
        *,rw,*)
            ;;
        *)
            return 1
            ;;
    esac

    [ -w "$DISK_ROOT" ]
}

sync_logs_once() {
    local src="$1"
    local dest="$2"
    local state_file="$3"
    local lock_file="$4"
    local last_pos=0
    local curr_size=0
    local lock_fd=""

    [ -f "$src" ] || return 0

    if ! is_storage_ready; then
        return 1
    fi

    mkdir -p "$(dirname "$dest")" 2>/dev/null || return 1

    exec {lock_fd}>"$lock_file" || return 1
    if ! flock -x "$lock_fd"; then
        exec {lock_fd}>&-
        return 1
    fi

    if [ -f "$state_file" ]; then
        last_pos="$(cat "$state_file" 2>/dev/null || echo 0)"
    fi
    curr_size="$(stat -c%s "$src" 2>/dev/null || echo 0)"

    if [ "$curr_size" -lt "$last_pos" ]; then
        last_pos=0
    fi

    if [ "$curr_size" -le "$last_pos" ]; then
        flock -u "$lock_fd" || true
        exec {lock_fd}>&-
        return 0
    fi

    if ! tail -c +$((last_pos + 1)) "$src" >> "$dest" 2>/dev/null; then
        flock -u "$lock_fd" || true
        exec {lock_fd}>&-
        return 1
    fi
    printf '%s' "$curr_size" > "$state_file"

    flock -u "$lock_fd" || true
    exec {lock_fd}>&-
}

cleanup() {
    sync_logs_once "$LOG_FILE_LOCAL" "$LOG_FILE_DISK" "$LOG_SYNC_POS_FILE" "$LOG_SYNC_LOCK_FILE" || true
    sleep 0.05
    sync_logs_once "$LOG_FILE_LOCAL" "$LOG_FILE_DISK" "$LOG_SYNC_POS_FILE" "$LOG_SYNC_LOCK_FILE" || true
    sleep 0.05
    sync_logs_once "$LOG_FILE_LOCAL" "$LOG_FILE_DISK" "$LOG_SYNC_POS_FILE" "$LOG_SYNC_LOCK_FILE" || true
}

mkdir -p "$LOG_DIR_LOCAL"
if [ ! -f "$LOG_WRITER" ]; then
    printf '[%s][tid %s][E][run_record %s] [run-record] log writer missing: %s error_code=UGR-70-01\n' \
        "$(date '+%s.%6N')" "$BASHPID" "$LINENO" "$LOG_WRITER" >&2
    exit 1
fi
exec > >(python3 "$LOG_WRITER" --output "$LOG_FILE_LOCAL") 2>&1
cleanup_old_logs

log_info "project_root=$project_root"
log_info "logging locally to: $LOG_FILE_LOCAL"

trap cleanup EXIT

cleanup_old_logs

runtime_exit_code=0
if "$RUNTIME_BIN" "$@"; then
    runtime_exit_code=0
else
    runtime_exit_code=$?
fi

exit "$runtime_exit_code"
