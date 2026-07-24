#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/board_test_common.sh"

SIDE="both"
PWM=500
DURATION_SEC=10
UGRIPPER_ROOT="$(default_ugripper_root)"
GRIPPER_HMI_BIN_OVERRIDE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --side) SIDE="$2"; shift 2 ;;
        --pwm) PWM="$2"; shift 2 ;;
        --duration-sec) DURATION_SEC="$2"; shift 2 ;;
        --ugripper-root) UGRIPPER_ROOT="$2"; shift 2 ;;
        --gripper-hmi-bin) GRIPPER_HMI_BIN_OVERRIDE="$2"; shift 2 ;;
        --help|-h)
            echo "Usage: $0 [--side left|right|both] [--pwm 0..1000] [--duration-sec N]"
            exit 0
            ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

if [[ ! "${PWM}" =~ ^[0-9]+$ ]] || (( PWM < 0 || PWM > 1000 )); then
    echo "PWM must be in range 0..1000" >&2
    exit 2
fi
if [[ ! "${DURATION_SEC}" =~ ^[0-9]+$ ]] || (( DURATION_SEC < 1 )); then
    echo "duration must be at least 1 second" >&2
    exit 2
fi

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

turn_off() {
    "${GRIPPER_HMI_BIN}" "${ports[@]}" --fill-light-pwm 0 --duration 1 --led-only >/dev/null 2>&1 || true
}
trap turn_off EXIT INT TERM

"${GRIPPER_HMI_BIN}" "${ports[@]}" --fill-light-pwm "${PWM}" --duration "${DURATION_SEC}" --led-only
