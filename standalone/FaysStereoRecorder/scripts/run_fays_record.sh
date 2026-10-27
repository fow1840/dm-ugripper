#!/bin/bash

# Get script directory and build directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$(dirname "$SCRIPT_DIR")"

CONFIG_FILE=""
EXECUTABLE="$BUILD_DIR/fays_record_example"
CMD_FIFO="/dev/shm/ugripper/umi_fays_cmd"
ARCH="$(uname -m)"
LOCAL_FAYS_LIB_DIR="$BUILD_DIR/lib/fays_atrak/${ARCH}/Release"
LOCAL_FTDI_LIB_DIR="$BUILD_DIR/lib/ft602-linux-${ARCH}"
VIDEO_NAME="fays_stereo_output.mkv"
MCAP_NAME="fays_data.mcap"
CALIB_JSON_PATH=""
STATUS_JSON_PATH=""
CMD_SEND_TIMEOUT_SEC="${FAYS_CMD_TIMEOUT_SEC:-0.35}"

fays_log() {
    printf '[%s][tid %s][I][%s %s] [fays_record_wrapper] %s\n' \
        "$(date '+%s.%6N')" "$BASHPID" "${FUNCNAME[1]:-main}" "${BASH_LINENO[0]:-0}" "$*" >&2
}

fays_error() {
    local code="$1"
    shift
    printf '[%s][tid %s][E][%s %s] [fays_record_wrapper] %s error_code=%s\n' \
        "$(date '+%s.%6N')" "$BASHPID" "${FUNCNAME[1]:-main}" "${BASH_LINENO[0]:-0}" "$*" "$code" >&2
}

append_ld_library_path() {
    local path_to_add="$1"
    if [ -d "$path_to_add" ]; then
        case ":$LD_LIBRARY_PATH:" in
            *":$path_to_add:"*) ;;
            *) export LD_LIBRARY_PATH="$path_to_add${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ;;
        esac
    else
        echo "Warning: Library directory not found: $path_to_add"
    fi
}

ensure_fixed_fays_symlinks() {
    local stereo_symlink="$1"
    local imu_symlink="$2"
    local missing=0

    if grep -Eq '^device_type:[[:space:]]*ft601[[:space:]]*$' "$CONFIG_FILE"; then
        [[ "$stereo_symlink" =~ ^[0-9]+-[0-9]+(\.[0-9]+)*$ ]] || return 1
        local physical="/sys/bus/usb/devices/$stereo_symlink"
        [ "$(cat "$physical/idVendor" 2>/dev/null)" = "0403" ] &&
            [ "$(cat "$physical/idProduct" 2>/dev/null)" = "601f" ] || return 1
        echo "Using FT601 shared image/IMU transport at $stereo_symlink"
        return 0
    fi

    if [ ! -e "$stereo_symlink" ]; then
        fays_error "UGR-22-01" "Error: Missing Fays stereo symlink: $stereo_symlink"
        missing=1
    fi

    if [ ! -e "$imu_symlink" ]; then
        fays_error "UGR-22-01" "Error: Missing Fays IMU symlink: $imu_symlink"
        missing=1
    fi

    if [ "$missing" -ne 0 ]; then
        echo "Hint: check udev rule camera_record/99-fixed-usb-map.rules and USB connection."
        return 1
    fi

    echo "Using fixed Fays symlink ports:"
    echo "  stereo_dev_port: $stereo_symlink -> $(readlink -f "$stereo_symlink")"
    echo "  imu_dev_port: $imu_symlink -> $(readlink -f "$imu_symlink")"

    return 0
}

verify_config_uses_fixed_symlinks() {
    local stereo_symlink="$1"
    local imu_symlink="$2"
    local stereo_cfg
    local imu_cfg

    if [ ! -f "$CONFIG_FILE" ]; then
        fays_error "UGR-70-01" "Error: Config file not found: $CONFIG_FILE"
        return 1
    fi

    stereo_cfg=$(sed -n 's/^stereo_dev_port:[[:space:]]*//p' "$CONFIG_FILE" | head -n1 | tr -d '"' | xargs)
    imu_cfg=$(sed -n 's/^imu_dev_port:[[:space:]]*//p' "$CONFIG_FILE" | head -n1 | tr -d '"' | xargs)

    if [ "$stereo_cfg" != "$stereo_symlink" ]; then
        fays_error "UGR-22-03" "Error: Config mismatch: stereo_dev_port=$stereo_cfg (expected $stereo_symlink)"
        return 1
    fi

    if [ "$imu_cfg" != "$imu_symlink" ]; then
        fays_error "UGR-22-03" "Error: Config mismatch: imu_dev_port=$imu_cfg (expected $imu_symlink)"
        return 1
    fi

    return 0
}

require_config_file() {
    if [ -z "$CONFIG_FILE" ]; then
        fays_error "UGR-70-01" "Error: --config FILE is required"
        return 1
    fi
    if [ ! -f "$CONFIG_FILE" ]; then
        fays_error "UGR-70-01" "Error: Config file not found: $CONFIG_FILE"
        return 1
    fi
    return 0
}

ensure_fifo() {
    mkdir -p "$(dirname "$CMD_FIFO")"
    if [ -e "$CMD_FIFO" ] && [ ! -p "$CMD_FIFO" ]; then
        fays_error "UGR-22-06" "Error: $CMD_FIFO exists but is not a FIFO"
        return 1
    fi
    if [ ! -p "$CMD_FIFO" ]; then
        mkfifo "$CMD_FIFO"
    fi
    return 0
}

send_control_cmd() {
    local cmd="$1"

    if [ ! -p "$CMD_FIFO" ]; then
        fays_error "UGR-22-06" "Error: Control FIFO not found: $CMD_FIFO"
        return 1
    fi

    if ! timeout "$CMD_SEND_TIMEOUT_SEC" bash -c 'printf "%s\n" "$1" > "$2"' _ "$cmd" "$CMD_FIFO" 2>/dev/null; then
        fays_error "UGR-22-06" "Error: Failed to send command '$cmd' via $CMD_FIFO"
        return 1
    fi

    fays_log "control command sent: fifo=$CMD_FIFO cmd=$cmd"
    return 0
}

filter_fays_runtime_log_stream() {
    awk '
        /^[[:space:]]*(Stereo FPS|IMU FPS):[[:space:]]*[0-9]+([.][0-9]+)?[[:space:]]*$/ { next }
        { print; fflush() }
    '
}

append_ld_library_path "$LOCAL_FAYS_LIB_DIR"
append_ld_library_path "$LOCAL_FTDI_LIB_DIR"
append_ld_library_path "/usr/local/lib"

if [ ! -f "$EXECUTABLE" ]; then
    fays_error "UGR-70-01" "Error: Executable '$EXECUTABLE' not found. Did you run cmake & make?"
    exit 1
fi

POSITIONAL=()
while [ "$#" -gt 0 ]; do
    case "$1" in
        --config)
            CONFIG_FILE="$2"
            shift 2
            ;;
        --control-fifo)
            CMD_FIFO="$2"
            shift 2
            ;;
        --video-name)
            VIDEO_NAME="$2"
            shift 2
            ;;
        --mcap-name)
            MCAP_NAME="$2"
            shift 2
            ;;
        --calib-json)
            CALIB_JSON_PATH="$2"
            shift 2
            ;;
        --status-json)
            STATUS_JSON_PATH="$2"
            shift 2
            ;;
        *)
            POSITIONAL+=("$1")
            shift
            ;;
    esac
done
set -- "${POSITIONAL[@]}"

MODE="${1:-}"
if [ "$MODE" = daemon ] || [ "$MODE" = dump-calib-json ]; then
    require_config_file || exit 1
    selection="$(python3 "$SCRIPT_DIR/select_fays_backend.py" "$CONFIG_FILE")" || exit 1
    backend="${selection%%$'\n'*}"
    CONFIG_FILE="${selection#*$'\n'}"
    if [ "$backend" = ft601 ]; then
        EXECUTABLE="$BUILD_DIR/fays_record_ft601"
        export LD_LIBRARY_PATH="$BUILD_DIR/lib/ft601/$ARCH:${LD_LIBRARY_PATH:-}"
    fi
fi

case "$MODE" in
    daemon)
        require_config_file || exit 1
        STEREO_SYMLINK=$(sed -n 's/^stereo_dev_port:[[:space:]]*//p' "$CONFIG_FILE" | head -n1 | tr -d '"' | xargs)
        IMU_SYMLINK=$(sed -n 's/^imu_dev_port:[[:space:]]*//p' "$CONFIG_FILE" | head -n1 | tr -d '"' | xargs)
        ensure_fixed_fays_symlinks "$STEREO_SYMLINK" "$IMU_SYMLINK" || exit 1
        verify_config_uses_fixed_symlinks "$STEREO_SYMLINK" "$IMU_SYMLINK" || exit 1
        fays_log "daemon exec: config=$CONFIG_FILE fifo=$CMD_FIFO video=$VIDEO_NAME mcap=$MCAP_NAME"
        args=("$CONFIG_FILE" --control-fifo "$CMD_FIFO" --video-name "$VIDEO_NAME" --mcap-name "$MCAP_NAME")
        if [ -n "$CALIB_JSON_PATH" ]; then
            args+=(--calib-json "$CALIB_JSON_PATH")
        fi
        if [ -n "$STATUS_JSON_PATH" ]; then
            args+=(--status-json "$STATUS_JSON_PATH")
        fi
        exec "$EXECUTABLE" "${args[@]}" \
            > >(filter_fays_runtime_log_stream) \
            2> >(filter_fays_runtime_log_stream >&2)
        ;;
    dump-calib-json)
        require_config_file || exit 1
        if [ -z "$CALIB_JSON_PATH" ]; then
            fays_error "UGR-70-01" "Error: dump-calib-json mode requires --calib-json PATH"
            exit 1
        fi
        STEREO_SYMLINK=$(sed -n 's/^stereo_dev_port:[[:space:]]*//p' "$CONFIG_FILE" | head -n1 | tr -d '"' | xargs)
        IMU_SYMLINK=$(sed -n 's/^imu_dev_port:[[:space:]]*//p' "$CONFIG_FILE" | head -n1 | tr -d '"' | xargs)
        ensure_fixed_fays_symlinks "$STEREO_SYMLINK" "$IMU_SYMLINK" || exit 1
        verify_config_uses_fixed_symlinks "$STEREO_SYMLINK" "$IMU_SYMLINK" || exit 1
        exec "$EXECUTABLE" "$CONFIG_FILE" --dump-calib-json "$CALIB_JSON_PATH"
        ;;
    start)
        OUTPUT_DIR="${2:-}"
        if [ -z "$OUTPUT_DIR" ]; then
            fays_error "UGR-22-06" "Error: start mode requires output directory"
            exit 1
        fi
        send_control_cmd "START|$OUTPUT_DIR"
        ;;
    stop)
        send_control_cmd "STOP"
        ;;
    exit)
        send_control_cmd "EXIT"
        ;;
    *)
        echo "Usage:"
        echo "  $0 [--config FILE] [--control-fifo FIFO] [--video-name NAME] [--mcap-name NAME] [--calib-json PATH] [--status-json PATH] daemon"
        echo "  $0 --config FILE --calib-json PATH dump-calib-json"
        echo "  $0 [--control-fifo FIFO] start <output_dir>"
        echo "  $0 [--control-fifo FIFO] stop"
        echo "  $0 [--control-fifo FIFO] exit"
        exit 1
        ;;
esac
