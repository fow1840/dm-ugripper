#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$SCRIPT_DIR/network_iface_lib.sh"
. "$SCRIPT_DIR/../scripts/lib/ugripper_shell_common.sh"

log_state() {
    [ "$*" = "${LAST_LOG_MESSAGE:-}" ] && return 0
    LAST_LOG_MESSAGE="$*"
    ugripper_log "network_monitor" "$1" "${@:2}"
}

UPGRADE_GUARD_FILE="/run/ugripper_installing_from_usb.lock"

is_upgrade_in_progress() {
    [ -f "$UPGRADE_GUARD_FILE" ]
}

CURRENT_INTERFACE=""
LAST_STATE=""
MONITOR_ARMED=0

arm_monitor() {
    local iface="$1"
    local state="$2"

    CURRENT_INTERFACE="$iface"
    LAST_STATE="$state"
    MONITOR_ARMED=1
    log_state INFO "Network Monitor armed on $CURRENT_INTERFACE. Baseline State: $LAST_STATE"
}

# ================= 信号处理 =================
cleanup() {
    log_state INFO "Network Monitor: Received stop signal. Exiting..."
    exit 0
}
trap cleanup SIGTERM SIGINT

# ================= 主循环 =================
while true; do
    DETECTED_INTERFACE=$(detect_preferred_ethernet_interface 2>/dev/null || true)
    if [ -z "$DETECTED_INTERFACE" ]; then
        if [ "$MONITOR_ARMED" -eq 1 ]; then
            log_state WARNING "Warning: no physical ethernet interface detected, keep last iface=$CURRENT_INTERFACE state=$LAST_STATE"
        else
            log_state INFO "Waiting for physical ethernet interface to appear..."
        fi
        sleep 1
        continue
    fi

    CURRENT_STATE=$(read_iface_carrier "$DETECTED_INTERFACE" 2>/dev/null || true)

    if [ -z "$CURRENT_STATE" ]; then
        log_state WARNING "Warning: interface $DETECTED_INTERFACE carrier not ready, keep last iface=${CURRENT_INTERFACE:-N/A} state=${LAST_STATE:-N/A}"
        sleep 1
        continue
    fi

    if [ "$MONITOR_ARMED" -ne 1 ]; then
        arm_monitor "$DETECTED_INTERFACE" "$CURRENT_STATE"
        sleep 1
        continue
    fi

    if [ "$CURRENT_INTERFACE" != "$DETECTED_INTERFACE" ]; then
        log_state INFO "Detected ethernet interface switch: ${CURRENT_INTERFACE:-N/A} -> $DETECTED_INTERFACE"
        arm_monitor "$DETECTED_INTERFACE" "$CURRENT_STATE"
        sleep 1
        continue
    fi

    if [ "$LAST_STATE" -ne "$CURRENT_STATE" ]; then
        if is_upgrade_in_progress; then
            log_state INFO "Upgrade guard active, skip network edge action on $CURRENT_INTERFACE: ${LAST_STATE} -> ${CURRENT_STATE}"
            LAST_STATE="$CURRENT_STATE"
            sleep 1
            continue
        fi

        # ================= 下降沿：插着 → 拔掉 =================
        if [ "$LAST_STATE" -eq 1 ] && [ "$CURRENT_STATE" -eq 0 ]; then
            log_state INFO "Cable removal detected on $CURRENT_INTERFACE"
            # Cable state never changes the recording service lifecycle.
        fi

        # ================= 上升沿：拔掉 → 插入 =================
        if [ "$LAST_STATE" -eq 0 ] && [ "$CURRENT_STATE" -eq 1 ]; then
            log_state INFO "Cable insertion detected on $CURRENT_INTERFACE, no action required"
        fi

        LAST_STATE="$CURRENT_STATE"
    fi

    sleep 1
done
