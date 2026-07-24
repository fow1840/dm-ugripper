#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/board_test_common.sh"

SIDE="both"
UGRIPPER_ROOT="$(default_ugripper_root)"
GRIPPER_HMI_BIN_OVERRIDE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --side) SIDE="$2"; shift 2 ;;
        --ugripper-root) UGRIPPER_ROOT="$2"; shift 2 ;;
        --gripper-hmi-bin) GRIPPER_HMI_BIN_OVERRIDE="$2"; shift 2 ;;
        --help|-h)
            echo "Usage: $0 [--side left|right|both] [--gripper-hmi-bin PATH]"
            exit 0
            ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

GRIPPER_HMI_BIN="$(resolve_executable "${GRIPPER_HMI_BIN_OVERRIDE}" \
    "${UGRIPPER_ROOT}/bin/GripperHmiTool/GripperHmiTool" \
    "${PPMAIN_ROOT}/build/x86/standalone_ros2/GripperHmiTool/GripperHmiTool")"

ports=()
case "${SIDE}" in
    left) ports+=(--port /dev/left_gripper) ;;
    right) ports+=(--port /dev/right_gripper) ;;
    both) ports+=(--port /dev/right_gripper --port /dev/left_gripper) ;;
    *) echo "side must be left, right, or both" >&2; exit 2 ;;
esac

exec "${GRIPPER_HMI_BIN}" "${ports[@]}" --monitor-lux --duration 0
