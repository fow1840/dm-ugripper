#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL="${SCRIPT_DIR}/GripperHmiTool"
if [[ ! -x "${TOOL}" ]]; then
    TOOL="/opt/ugripper/bin/GripperHmiTool/GripperHmiTool"
fi
if [[ ! -x "${TOOL}" ]]; then
    echo "GripperHmiTool not found" >&2
    exit 1
fi

has_port=0
for argument in "$@"; do
    if [[ "${argument}" == "--port" ]]; then
        has_port=1
        break
    fi
done

args=(--monitor-lux --duration 0)
if [[ "${has_port}" == "0" ]]; then
    args+=(--port /dev/right_gripper --port /dev/left_gripper)
fi

exec "${TOOL}" "${args[@]}" "$@"
