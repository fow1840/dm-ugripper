#!/usr/bin/env bash
#
# UGripper 外部秒表联动录制测试
#
# 在 PC 上执行本脚本。脚本只校验目标板现有 UGripper 已生成的 CH9344 串口
# 映射，不安装 helper，也不重放 udev。随后通过运行时控制 FIFO 启动一次正常
# 的 UGripper 录制；等待指定延迟后
# 通过 GPIO139 拉低并持续触发外部秒表。测试前先保持高电平 5 秒清零；达到
# 录制总时长后停止录制，再恢复高电平并保持 5 秒，使秒表停止并清零，最后
# 向最终 episode 写入 stopwatch_timing.json。
#
# PC 端前置条件：
#   - 已安装 bash、ssh、scp 和 sshpass
#   - 能通过网络访问目标板端
#
# 板端前置条件：
#   - ugripper.service 已启动且处于可录制状态
#   - /mnt/data_disk 已挂载且可写
#   - 外部秒表触发线已接入 GPIO139
#   - GPIO 低电平持续计时，高电平持续至少 5 秒清零
#
# 基本用法：
#   ./test/scripts/board_stopwatch_record_test.sh
#   ./test/scripts/board_stopwatch_record_test.sh --duration-sec 30
#   ./test/scripts/board_stopwatch_record_test.sh --host 192.168.2.240 --gpio 139
#
# 常用参数：
#   --host HOST                  板端地址（默认：192.168.2.240）
#   --user USER                  SSH 用户名（默认：ubuntu）
#   --password PASSWORD          SSH/sudo 密码（默认：ubuntu）
#   --duration-sec N             录制总持续时长，单位秒（默认：15）
#   --stopwatch-delay-sec N      录制 START 后等待多久再触发秒表，单位秒（默认：2）
#   --reset-hold-sec N           清零时高电平保持时长，至少 5 秒（默认：5）
#   --gpio N                     legacy sysfs GPIO 编号（默认：139）
#   --active-level 0             秒表持续运行电平；当前硬件固定为低电平
#   --episode-root DIR           板端 episode 根目录；默认自动识别
#   --start-timeout-sec N        等待新 episode 创建的超时时间，单位秒（默认：30）
#   --finalize-timeout-sec N     等待 episode 收尾完成的超时时间，单位秒（默认：180）
#
# 输出：
#   最终 episode 会新增 stopwatch_timing.json，其中包含预打开 GPIO fd 后的
#   微秒级触发/停止边界、前后高电平清零保持时间、START/STOP FIFO 时间戳、
#   指定时长和 episode 路径。
#
# CH9344 串口保护：
#   - 只校验板端现有映射，不覆盖 helper、不 reload/replay udev。
#   - 校验四个逻辑链接均只有一个声明者，且 symlink 指向该声明者，
#     接受板端正式 UGripper helper 已生成的有效映射。
#   - 校验失败时不会启动秒表或录制。
#
# 安全机制：
#   GPIO 初始化会原子设置输出方向和空闲电平，避免低电平有效场景在正式触发前
#   因 direction=out 的瞬时低电平误启动秒表。
#   初始化确认后由单个常驻 Python helper 预打开 GPIO value fd；open 不写电平，
#   正式触发只执行 clock_gettime -> write -> clock_gettime，不再启动 sudo/tee/date。
#   START 后如遇中断或失败，板端 trap 会发送 STOP，将 GPIO 恢复为高电平并
#   保持完整清零时长后再退出。
#   如已存在录制锁，脚本会拒绝开始新测试。

set -euo pipefail

SCRIPT_NAME="$(basename "${BASH_SOURCE[0]}")"
SCRIPT_VERSION="11"

DEFAULT_HOST="192.168.2.240"
DEFAULT_USER="ubuntu"
DEFAULT_PASSWORD="ubuntu"
DEFAULT_DURATION_SEC=15
DEFAULT_STOPWATCH_DELAY_SEC=2
DEFAULT_RESET_HOLD_SEC=5
DEFAULT_GPIO=139
DEFAULT_ACTIVE_LEVEL=0
DEFAULT_START_TIMEOUT_SEC=30
DEFAULT_FINALIZE_TIMEOUT_SEC=180

RECORD_CONTROL_PIPE="/dev/shm/ugripper/umi_record_control.pipe"
RECORDING_LOCK="/tmp/umi_recording.lock"
SERVICE_NAME="ugripper.service"
TIMING_FILE_NAME="stopwatch_timing.json"

usage() {
    sed -n '2,/^set -euo pipefail$/p' "${BASH_SOURCE[0]}" | sed '$d; s/^# \{0,1\}//'
}

die() {
    echo "ERROR: $*" >&2
    exit 1
}

require_positive_integer() {
    local name="$1"
    local value="$2"
    [[ "${value}" =~ ^[1-9][0-9]*$ ]] || die "${name} must be a positive integer: ${value}"
}

require_nonnegative_integer() {
    local name="$1"
    local value="$2"
    [[ "${value}" =~ ^[0-9]+$ ]] || die "${name} must be a non-negative integer: ${value}"
}

shell_join() {
    local output=""
    local quoted=""
    local value
    for value in "$@"; do
        printf -v quoted '%q' "${value}"
        output+="${quoted} "
    done
    printf '%s' "${output}"
}

find_episode_root() {
    local explicit_root="$1"
    if [[ -n "${explicit_root}" ]]; then
        [[ -d "${explicit_root}" ]] || die "episode root does not exist: ${explicit_root}"
        printf '%s\n' "${explicit_root}"
        return
    fi

    local roots=()
    local candidate
    for candidate in /mnt/data_disk/*/data; do
        [[ -d "${candidate}" ]] || continue
        roots+=("${candidate}")
    done

    if [[ "${#roots[@]}" -eq 1 ]]; then
        printf '%s\n' "${roots[0]}"
        return
    fi
    if [[ "${#roots[@]}" -eq 0 ]]; then
        die "no episode root found under /mnt/data_disk/*/data"
    fi

    die "multiple episode roots found; pass --episode-root explicitly: ${roots[*]}"
}

capture_time() {
    local prefix="$1"
    local unix_ns
    local monotonic_sec
    unix_ns="$(date +%s%N)"
    read -r monotonic_sec _ < /proc/uptime
    printf -v "${prefix}_unix_ns" '%s' "${unix_ns}"
    printf -v "${prefix}_monotonic_sec" '%s' "${monotonic_sec}"
}

start_gpio_helper() {
    local python_bin=""
    local candidate
    for candidate in /usr/bin/python3 /opt/ugripper/.venv/bin/python3 python3; do
        if command -v "${candidate}" >/dev/null 2>&1 || [[ -x "${candidate}" ]]; then
            python_bin="${candidate}"
            break
        fi
    done
    [[ -n "${python_bin}" ]] || die "python3 is required for precise GPIO triggering"

    local gpio_helper_code
    read -r -d '' gpio_helper_code <<'PY' || true
import os
import sys
import time


gpio_path = sys.argv[1]
write_fd = os.open(gpio_path, os.O_WRONLY | os.O_CLOEXEC)
read_fd = os.open(gpio_path, os.O_RDONLY | os.O_CLOEXEC)


def read_value():
    os.lseek(read_fd, 0, os.SEEK_SET)
    return os.read(read_fd, 8).strip().decode("ascii")


print(f"READY\t{read_value()}", flush=True)
try:
    for raw_line in sys.stdin:
        fields = raw_line.rstrip("\n").split("\t")
        command = fields[0]
        if command == "CLOSE":
            print("BYE", flush=True)
            break
        if command != "WRITE" or len(fields) != 2 or fields[1] not in ("0", "1"):
            print("ERROR\tinvalid command", flush=True)
            continue

        value = fields[1]
        before_unix_ns = time.clock_gettime_ns(time.CLOCK_REALTIME)
        before_monotonic_ns = time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW)
        os.lseek(write_fd, 0, os.SEEK_SET)
        written = os.write(write_fd, value.encode("ascii"))
        after_monotonic_ns = time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW)
        after_unix_ns = time.clock_gettime_ns(time.CLOCK_REALTIME)
        if written != 1:
            print(f"ERROR\tshort write: {written}", flush=True)
            continue
        actual = read_value()
        for _ in range(100):
            if actual == value:
                break
            actual = read_value()
        print(
            "\t".join(
                [
                    "OK",
                    value,
                    actual,
                    str(before_unix_ns),
                    str(after_unix_ns),
                    str(before_monotonic_ns),
                    str(after_monotonic_ns),
                ]
            ),
            flush=True,
        )
finally:
    os.close(read_fd)
    os.close(write_fd)
PY

    coproc GPIO_TRIGGER_HELPER {
        exec sudo -n "${python_bin}" -c "${gpio_helper_code}" "${GPIO_VALUE_PATH}"
    }
    GPIO_HELPER_READ_FD="${GPIO_TRIGGER_HELPER[0]}"
    GPIO_HELPER_WRITE_FD="${GPIO_TRIGGER_HELPER[1]}"
    GPIO_HELPER_PID="${GPIO_TRIGGER_HELPER_PID}"

    local status
    local actual
    IFS=$'\t' read -r status actual <&"${GPIO_HELPER_READ_FD}" || die "precise GPIO helper exited during startup"
    [[ "${status}" == "READY" ]] || die "precise GPIO helper startup failed: ${status} ${actual}"
    [[ "${actual}" == "${IDLE_LEVEL}" ]] || die "GPIO${GPIO} changed while opening helper: expected ${IDLE_LEVEL}, got ${actual}"
}

write_gpio_precise() {
    local prefix="$1"
    local value="$2"
    local status
    local requested
    local actual
    local before_unix_ns
    local after_unix_ns
    local before_monotonic_ns
    local after_monotonic_ns

    printf 'WRITE\t%s\n' "${value}" >&"${GPIO_HELPER_WRITE_FD}" || return 1
    IFS=$'\t' read -r status requested actual before_unix_ns after_unix_ns \
        before_monotonic_ns after_monotonic_ns <&"${GPIO_HELPER_READ_FD}" || return 1
    if [[ "${status}" != "OK" ]]; then
        echo "ERROR: precise GPIO helper write failed: ${status} ${requested} ${actual}" >&2
        return 1
    fi
    if [[ "${requested}" != "${value}" || "${actual}" != "${value}" ]]; then
        echo "ERROR: GPIO${GPIO} readback mismatch: wrote ${value}, read ${actual}" >&2
        return 1
    fi

    printf -v "${prefix}_before_unix_ns" '%s' "${before_unix_ns}"
    printf -v "${prefix}_after_unix_ns" '%s' "${after_unix_ns}"
    printf -v "${prefix}_before_monotonic_ns" '%s' "${before_monotonic_ns}"
    printf -v "${prefix}_after_monotonic_ns" '%s' "${after_monotonic_ns}"
}

stop_gpio_helper() {
    [[ -n "${GPIO_HELPER_PID:-}" ]] || return 0
    if kill -0 "${GPIO_HELPER_PID}" 2>/dev/null; then
        printf 'CLOSE\n' >&"${GPIO_HELPER_WRITE_FD}" 2>/dev/null || true
        local status=""
        IFS=$'\t' read -r status <&"${GPIO_HELPER_READ_FD}" 2>/dev/null || true
        [[ "${status}" == "BYE" ]] || true
        wait "${GPIO_HELPER_PID}" 2>/dev/null || true
    fi
    GPIO_HELPER_PID=""
}

snapshot_episode_names() {
    find "${EPISODE_ROOT}" -maxdepth 1 -mindepth 1 -type d -name 'episode_*' -printf '%f\n' 2>/dev/null | sort
}

monitor_new_episode() {
    local name
    while [[ ! -e "${MONITOR_STOP_FILE}" ]]; do
        while IFS= read -r name; do
            [[ -n "${name}" ]] || continue
            if ! grep -Fqx -- "${name}" "${BASELINE_FILE}"; then
                local observed_unix_ns
                local observed_monotonic_sec
                observed_unix_ns="$(date +%s%N)"
                read -r observed_monotonic_sec _ < /proc/uptime
                printf '%s\n' "${EPISODE_ROOT}/${name}" > "${NEW_EPISODE_FILE}.tmp"
                mv "${NEW_EPISODE_FILE}.tmp" "${NEW_EPISODE_FILE}"
                printf '%s %s\n' "${observed_unix_ns}" "${observed_monotonic_sec}" > "${EPISODE_OBSERVED_FILE}.tmp"
                mv "${EPISODE_OBSERVED_FILE}.tmp" "${EPISODE_OBSERVED_FILE}"
                return 0
            fi
        done < <(snapshot_episode_names)
        sleep 0.1
    done
    return 1
}

wait_for_final_episode() {
    local observed_episode="$1"
    local final_episode="${observed_episode}"
    if [[ "${final_episode}" == *-temp ]]; then
        final_episode="${final_episode%-temp}"
    fi

    local deadline=$((SECONDS + FINALIZE_TIMEOUT_SEC))
    while (( SECONDS < deadline )); do
        if [[ -d "${final_episode}" && ! -e "${RECORDING_LOCK}" ]]; then
            printf '%s\n' "${final_episode}"
            return 0
        fi
        sleep 1
    done

    die "timed out waiting for finalized episode: ${final_episode}"
}

write_timing_json() {
    local final_episode="$1"
    local python_bin=""
    local candidate
    for candidate in /usr/bin/python3 /opt/ugripper/.venv/bin/python3 python3; do
        if command -v "${candidate}" >/dev/null 2>&1 || [[ -x "${candidate}" ]]; then
            python_bin="${candidate}"
            break
        fi
    done
    [[ -n "${python_bin}" ]] || die "python3 is required to write ${TIMING_FILE_NAME}"

    export UGRIPPER_STOPWATCH_SCRIPT_VERSION="${SCRIPT_VERSION}"
    export UGRIPPER_STOPWATCH_TARGET_LABEL="${TARGET_LABEL}"
    export UGRIPPER_STOPWATCH_EPISODE="${final_episode}"
    export UGRIPPER_STOPWATCH_DURATION_SEC="${DURATION_SEC}"
    export UGRIPPER_STOPWATCH_DELAY_SEC="${STOPWATCH_DELAY_SEC}"
    export UGRIPPER_STOPWATCH_RESET_HOLD_SEC="${RESET_HOLD_SEC}"
    export UGRIPPER_STOPWATCH_GPIO="${GPIO}"
    export UGRIPPER_STOPWATCH_ACTIVE_LEVEL="${ACTIVE_LEVEL}"
    export UGRIPPER_STOPWATCH_IDLE_LEVEL="${IDLE_LEVEL}"
    export UGRIPPER_STOPWATCH_PRE_RESET_START_UNIX_NS="${pre_reset_start_unix_ns}"
    export UGRIPPER_STOPWATCH_PRE_RESET_START_MONOTONIC_SEC="${pre_reset_start_monotonic_sec}"
    export UGRIPPER_STOPWATCH_PRE_RESET_COMPLETE_UNIX_NS="${pre_reset_complete_unix_ns}"
    export UGRIPPER_STOPWATCH_PRE_RESET_COMPLETE_MONOTONIC_SEC="${pre_reset_complete_monotonic_sec}"
    export UGRIPPER_STOPWATCH_GPIO_START_BEFORE_UNIX_NS="${gpio_start_before_unix_ns}"
    export UGRIPPER_STOPWATCH_GPIO_START_BEFORE_MONOTONIC_NS="${gpio_start_before_monotonic_ns}"
    export UGRIPPER_STOPWATCH_GPIO_START_AFTER_UNIX_NS="${gpio_start_after_unix_ns}"
    export UGRIPPER_STOPWATCH_GPIO_START_AFTER_MONOTONIC_NS="${gpio_start_after_monotonic_ns}"
    export UGRIPPER_STOPWATCH_RECORD_START_BEFORE_UNIX_NS="${record_start_before_unix_ns}"
    export UGRIPPER_STOPWATCH_RECORD_START_BEFORE_MONOTONIC_SEC="${record_start_before_monotonic_sec}"
    export UGRIPPER_STOPWATCH_RECORD_START_AFTER_UNIX_NS="${record_start_after_unix_ns}"
    export UGRIPPER_STOPWATCH_RECORD_START_AFTER_MONOTONIC_SEC="${record_start_after_monotonic_sec}"
    export UGRIPPER_STOPWATCH_EPISODE_OBSERVED_UNIX_NS="${episode_observed_unix_ns}"
    export UGRIPPER_STOPWATCH_EPISODE_OBSERVED_MONOTONIC_SEC="${episode_observed_monotonic_sec}"
    export UGRIPPER_STOPWATCH_RECORD_STOP_BEFORE_UNIX_NS="${record_stop_before_unix_ns}"
    export UGRIPPER_STOPWATCH_RECORD_STOP_BEFORE_MONOTONIC_SEC="${record_stop_before_monotonic_sec}"
    export UGRIPPER_STOPWATCH_RECORD_STOP_AFTER_UNIX_NS="${record_stop_after_unix_ns}"
    export UGRIPPER_STOPWATCH_RECORD_STOP_AFTER_MONOTONIC_SEC="${record_stop_after_monotonic_sec}"
    export UGRIPPER_STOPWATCH_GPIO_STOP_BEFORE_UNIX_NS="${gpio_stop_before_unix_ns}"
    export UGRIPPER_STOPWATCH_GPIO_STOP_BEFORE_MONOTONIC_NS="${gpio_stop_before_monotonic_ns}"
    export UGRIPPER_STOPWATCH_GPIO_STOP_AFTER_UNIX_NS="${gpio_stop_after_unix_ns}"
    export UGRIPPER_STOPWATCH_GPIO_STOP_AFTER_MONOTONIC_NS="${gpio_stop_after_monotonic_ns}"
    export UGRIPPER_STOPWATCH_RESET_COMPLETE_UNIX_NS="${reset_complete_unix_ns}"
    export UGRIPPER_STOPWATCH_RESET_COMPLETE_MONOTONIC_SEC="${reset_complete_monotonic_sec}"

    "${python_bin}" - "${final_episode}/${TIMING_FILE_NAME}" <<'PY'
import datetime
import json
import os
import pathlib
import tempfile
from decimal import Decimal


def env(name):
    return os.environ[f"UGRIPPER_STOPWATCH_{name}"]


def ns(name):
    return int(env(name))


def mono_ns(name):
    return int(Decimal(env(name)) * Decimal(1_000_000_000))


def iso8601(unix_ns):
    seconds, nanos = divmod(unix_ns, 1_000_000_000)
    base = datetime.datetime.fromtimestamp(seconds, datetime.timezone.utc)
    return f"{base:%Y-%m-%dT%H:%M:%S}.{nanos:09d}Z"


gpio_start_before = ns("GPIO_START_BEFORE_UNIX_NS")
gpio_start_after = ns("GPIO_START_AFTER_UNIX_NS")
pre_reset_start = ns("PRE_RESET_START_UNIX_NS")
pre_reset_complete = ns("PRE_RESET_COMPLETE_UNIX_NS")
record_start_before = ns("RECORD_START_BEFORE_UNIX_NS")
record_start_after = ns("RECORD_START_AFTER_UNIX_NS")
episode_observed = ns("EPISODE_OBSERVED_UNIX_NS")
record_stop_before = ns("RECORD_STOP_BEFORE_UNIX_NS")
record_stop_after = ns("RECORD_STOP_AFTER_UNIX_NS")
gpio_stop_before = ns("GPIO_STOP_BEFORE_UNIX_NS")
gpio_stop_after = ns("GPIO_STOP_AFTER_UNIX_NS")
reset_complete = ns("RESET_COMPLETE_UNIX_NS")

gpio_start_before_mono = ns("GPIO_START_BEFORE_MONOTONIC_NS")
gpio_start_after_mono = ns("GPIO_START_AFTER_MONOTONIC_NS")
pre_reset_start_mono = mono_ns("PRE_RESET_START_MONOTONIC_SEC")
pre_reset_complete_mono = mono_ns("PRE_RESET_COMPLETE_MONOTONIC_SEC")
record_start_before_mono = mono_ns("RECORD_START_BEFORE_MONOTONIC_SEC")
record_start_after_mono = mono_ns("RECORD_START_AFTER_MONOTONIC_SEC")
episode_observed_mono = mono_ns("EPISODE_OBSERVED_MONOTONIC_SEC")
record_stop_before_mono = mono_ns("RECORD_STOP_BEFORE_MONOTONIC_SEC")
record_stop_after_mono = mono_ns("RECORD_STOP_AFTER_MONOTONIC_SEC")
gpio_stop_before_mono = ns("GPIO_STOP_BEFORE_MONOTONIC_NS")
gpio_stop_after_mono = ns("GPIO_STOP_AFTER_MONOTONIC_NS")
reset_complete_mono = mono_ns("RESET_COMPLETE_MONOTONIC_SEC")

gpio_start_estimate = (gpio_start_before + gpio_start_after) // 2
gpio_stop_estimate = (gpio_stop_before + gpio_stop_after) // 2

payload = {
    "schema_version": 2,
    "test": "normal_recording_then_external_stopwatch",
    "script_version": env("SCRIPT_VERSION"),
    "target": env("TARGET_LABEL"),
    "episode_path": env("EPISODE"),
    "requested_duration_sec": int(env("DURATION_SEC")),
    "requested_stopwatch_delay_sec": int(env("DELAY_SEC")),
    "requested_reset_hold_sec": int(env("RESET_HOLD_SEC")),
    "gpio": {
        "number": int(env("GPIO")),
        "active_level": int(env("ACTIVE_LEVEL")),
        "idle_level": int(env("IDLE_LEVEL")),
        "trigger_method": "persistent_preopened_sysfs_fd",
    },
    "events": {
        "pre_record_reset_hold_start": {
            "unix_ns": pre_reset_start,
            "iso8601_utc": iso8601(pre_reset_start),
            "monotonic_ns": pre_reset_start_mono,
        },
        "pre_record_reset_complete": {
            "unix_ns": pre_reset_complete,
            "iso8601_utc": iso8601(pre_reset_complete),
            "monotonic_ns": pre_reset_complete_mono,
        },
        "stopwatch_start_write_before": {
            "unix_ns": gpio_start_before,
            "iso8601_utc": iso8601(gpio_start_before),
            "monotonic_ns": gpio_start_before_mono,
        },
        "stopwatch_start_write_after": {
            "unix_ns": gpio_start_after,
            "iso8601_utc": iso8601(gpio_start_after),
            "monotonic_ns": gpio_start_after_mono,
        },
        "stopwatch_start_estimate": {
            "unix_ns": gpio_start_estimate,
            "iso8601_utc": iso8601(gpio_start_estimate),
        },
        "record_start_command_before": {
            "unix_ns": record_start_before,
            "iso8601_utc": iso8601(record_start_before),
            "monotonic_ns": record_start_before_mono,
        },
        "record_start_command_after": {
            "unix_ns": record_start_after,
            "iso8601_utc": iso8601(record_start_after),
            "monotonic_ns": record_start_after_mono,
        },
        "episode_first_observed": {
            "unix_ns": episode_observed,
            "iso8601_utc": iso8601(episode_observed),
            "monotonic_ns": episode_observed_mono,
        },
        "record_stop_command_before": {
            "unix_ns": record_stop_before,
            "iso8601_utc": iso8601(record_stop_before),
            "monotonic_ns": record_stop_before_mono,
        },
        "record_stop_command_after": {
            "unix_ns": record_stop_after,
            "iso8601_utc": iso8601(record_stop_after),
            "monotonic_ns": record_stop_after_mono,
        },
        "stopwatch_stop_write_before": {
            "unix_ns": gpio_stop_before,
            "iso8601_utc": iso8601(gpio_stop_before),
            "monotonic_ns": gpio_stop_before_mono,
        },
        "stopwatch_stop_write_after": {
            "unix_ns": gpio_stop_after,
            "iso8601_utc": iso8601(gpio_stop_after),
            "monotonic_ns": gpio_stop_after_mono,
        },
        "stopwatch_stop_estimate": {
            "unix_ns": gpio_stop_estimate,
            "iso8601_utc": iso8601(gpio_stop_estimate),
        },
        "stopwatch_reset_complete": {
            "unix_ns": reset_complete,
            "iso8601_utc": iso8601(reset_complete),
            "monotonic_ns": reset_complete_mono,
        },
    },
    "measurements": {
        "clock": "CLOCK_REALTIME_ns",
        "pre_record_high_hold_ms": (pre_reset_complete - pre_reset_start) / 1_000_000,
        "stopwatch_start_write_window_ms": (gpio_start_after - gpio_start_before) / 1_000_000,
        "stopwatch_start_write_window_us": (gpio_start_after - gpio_start_before) / 1_000,
        "record_start_write_window_ms": (record_start_after - record_start_before) / 1_000_000,
        "record_start_command_to_stopwatch_start_min_ms": (gpio_start_before - record_start_after) / 1_000_000,
        "record_start_command_to_stopwatch_start_max_ms": (gpio_start_after - record_start_before) / 1_000_000,
        "record_start_command_to_episode_observed_ms": (episode_observed - record_start_after) / 1_000_000,
        "record_command_window_ms": (record_stop_before - record_start_after) / 1_000_000,
        "record_stop_write_window_ms": (record_stop_after - record_stop_before) / 1_000_000,
        "record_stop_command_to_stopwatch_stop_min_ms": (gpio_stop_before - record_stop_after) / 1_000_000,
        "record_stop_command_to_stopwatch_stop_max_ms": (gpio_stop_after - record_stop_before) / 1_000_000,
        "stopwatch_stop_write_window_ms": (gpio_stop_after - gpio_stop_before) / 1_000_000,
        "stopwatch_stop_write_window_us": (gpio_stop_after - gpio_stop_before) / 1_000,
        "stopwatch_active_estimate_ms": (gpio_stop_estimate - gpio_start_estimate) / 1_000_000,
        "stopwatch_active_min_ms": (gpio_stop_before - gpio_start_after) / 1_000_000,
        "stopwatch_active_max_ms": (gpio_stop_after - gpio_start_before) / 1_000_000,
        "post_stop_high_hold_ms": (reset_complete - gpio_stop_after) / 1_000_000,
    },
    "notes": [
        "The sysfs write() occurred between each write_before/write_after timestamp pair; an oscilloscope is required to verify the physical pin edge.",
        "The estimate event is the midpoint of its write_before/write_after interval.",
        "GPIO trigger timestamps are captured inside one persistent root helper around write() on a pre-opened sysfs value fd.",
        "CLOCK_REALTIME provides Unix timestamps; CLOCK_MONOTONIC_RAW timestamps are retained as raw diagnostic evidence.",
        "GPIO direction and idle level were initialized atomically before the trigger to avoid an active-low initialization glitch.",
        "GPIO was held high for the requested reset interval before recording and again after STOP so the stopwatch could clear.",
        "START and STOP were sent through the normal UGripper runtime recording control FIFO.",
        "Recording START was sent before the stopwatch trigger; the configured delay is included in requested_stopwatch_delay_sec.",
    ],
}

destination = pathlib.Path(os.sys.argv[1])
destination.parent.mkdir(parents=True, exist_ok=True)
fd, temp_name = tempfile.mkstemp(prefix=f".{destination.name}.", dir=destination.parent)
try:
    with os.fdopen(fd, "w", encoding="utf-8") as stream:
        json.dump(payload, stream, ensure_ascii=True, indent=2)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temp_name, destination)
finally:
    try:
        os.unlink(temp_name)
    except FileNotFoundError:
        pass
PY
}

device_devlinks() {
    local node="$1"
    udevadm info -q property -n "${node}" | sed -n 's/^DEVLINKS=//p'
}

devlinks_contain() {
    local devlinks="$1"
    local expected="$2"
    local link
    for link in ${devlinks}; do
        [[ "${link}" == "${expected}" ]] && return 0
    done
    return 1
}

validate_logical_ch9344_link() {
    local logical_name="$1"
    local count=0
    local claimant=""
    local node
    local devlinks

    for node in /dev/ttyCH9344USB*; do
        [[ -e "${node}" ]] || continue
        devlinks="$(device_devlinks "${node}")"
        if devlinks_contain "${devlinks}" "/dev/${logical_name}"; then
            count=$((count + 1))
            claimant="${node}"
        fi
    done

    [[ "${count}" -eq 1 ]] || die "/dev/${logical_name} must have exactly one CH9344 claimant, found ${count}"
    [[ -L "/dev/${logical_name}" ]] || die "/dev/${logical_name} is not an active udev symlink"
    [[ "$(readlink -f "/dev/${logical_name}")" == "$(readlink -f "${claimant}")" ]] || \
        die "/dev/${logical_name} does not resolve to its sole claimant ${claimant}"

    local device_path
    device_path="$(udevadm info -q path -n "${claimant}")"
    echo "/dev/${logical_name} -> ${claimant} (${device_path})"
}

validate_ch9344_mapping() {
    echo "Validating existing CH9344 logical links without enforcing physical bus numbers"
    validate_logical_ch9344_link right_gripper
    validate_logical_ch9344_link right_encoder
    validate_logical_ch9344_link left_gripper
    validate_logical_ch9344_link left_encoder
    echo "Existing CH9344 mapping validation passed"
}

board_cleanup() {
    local exit_code=$?
    trap - EXIT HUP INT TERM
    set +e
    [[ -n "${BOARD_SLEEP_PID:-}" ]] && kill "${BOARD_SLEEP_PID}" 2>/dev/null

    [[ -n "${MONITOR_PID:-}" ]] && kill "${MONITOR_PID}" 2>/dev/null
    if [[ "${RECORD_START_SENT:-0}" == "1" && "${RECORD_STOP_SENT:-0}" != "1" && -p "${RECORD_CONTROL_PIPE}" ]]; then
        printf 'STOP\n' > "${RECORD_CONTROL_PIPE}"
        RECORD_STOP_SENT=1
    fi
    if [[ "${GPIO_ACTIVE:-0}" == "1" && -e "/sys/class/gpio/gpio${GPIO:-}/value" ]]; then
        if ! write_gpio_precise gpio_cleanup "${IDLE_LEVEL}"; then
            printf '%s\n' "${IDLE_LEVEL}" | sudo -n tee "/sys/class/gpio/gpio${GPIO}/value" >/dev/null
        fi
        GPIO_ACTIVE=0
    fi
    if [[ "${RESET_HOLD_REQUIRED:-0}" == "1" && "${STOPWATCH_RESET_COMPLETED:-0}" != "1" ]]; then
        echo "Waiting ${RESET_HOLD_SEC}s at high level so the stopwatch can reset" >&2
        sleep "${RESET_HOLD_SEC}"
        STOPWATCH_RESET_COMPLETED=1
    fi
    stop_gpio_helper
    [[ -n "${WORK_DIR:-}" && -d "${WORK_DIR}" ]] && rm -rf "${WORK_DIR}"

    exit "${exit_code}"
}

# Background sleep lets Bash handle TERM immediately, preserving the GPIO helper
# for the cleanup trap instead of waiting for a long foreground sleep to finish.
board_sleep() {
    sleep "$1" &
    BOARD_SLEEP_PID=$!
    wait "${BOARD_SLEEP_PID}"
    BOARD_SLEEP_PID=""
}

run_board() {
    DURATION_SEC="${DEFAULT_DURATION_SEC}"
    STOPWATCH_DELAY_SEC="${DEFAULT_STOPWATCH_DELAY_SEC}"
    RESET_HOLD_SEC="${DEFAULT_RESET_HOLD_SEC}"
    GPIO="${DEFAULT_GPIO}"
    ACTIVE_LEVEL="${DEFAULT_ACTIVE_LEVEL}"
    START_TIMEOUT_SEC="${DEFAULT_START_TIMEOUT_SEC}"
    FINALIZE_TIMEOUT_SEC="${DEFAULT_FINALIZE_TIMEOUT_SEC}"
    EPISODE_ROOT_OVERRIDE=""
    TARGET_LABEL="unknown"

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --duration-sec) DURATION_SEC="$2"; shift 2 ;;
            --stopwatch-delay-sec) STOPWATCH_DELAY_SEC="$2"; shift 2 ;;
            --reset-hold-sec) RESET_HOLD_SEC="$2"; shift 2 ;;
            --gpio) GPIO="$2"; shift 2 ;;
            --active-level) ACTIVE_LEVEL="$2"; shift 2 ;;
            --episode-root) EPISODE_ROOT_OVERRIDE="$2"; shift 2 ;;
            --start-timeout-sec) START_TIMEOUT_SEC="$2"; shift 2 ;;
            --finalize-timeout-sec) FINALIZE_TIMEOUT_SEC="$2"; shift 2 ;;
            --target-label) TARGET_LABEL="$2"; shift 2 ;;
            *) die "unknown board argument: $1" ;;
        esac
    done

    require_positive_integer "duration-sec" "${DURATION_SEC}"
    require_nonnegative_integer "stopwatch-delay-sec" "${STOPWATCH_DELAY_SEC}"
    require_positive_integer "reset-hold-sec" "${RESET_HOLD_SEC}"
    (( RESET_HOLD_SEC >= 5 )) || die "reset-hold-sec must be at least 5 seconds"
    (( STOPWATCH_DELAY_SEC < DURATION_SEC )) || die "stopwatch-delay-sec must be less than duration-sec"
    require_nonnegative_integer "gpio" "${GPIO}"
    require_positive_integer "start-timeout-sec" "${START_TIMEOUT_SEC}"
    require_positive_integer "finalize-timeout-sec" "${FINALIZE_TIMEOUT_SEC}"
    [[ "${ACTIVE_LEVEL}" == "0" ]] || die "active-level must be 0 for the current low-run/high-reset stopwatch"
    IDLE_LEVEL=$((1 - ACTIVE_LEVEL))

    command -v sudo >/dev/null 2>&1 || die "sudo is required on the board"
    sudo -n true || die "sudo credentials are not cached; PC-side launcher must validate sudo first"
    validate_ch9344_mapping
    systemctl is-active --quiet "${SERVICE_NAME}" || die "${SERVICE_NAME} is not active"
    [[ -p "${RECORD_CONTROL_PIPE}" ]] || die "record control FIFO is missing: ${RECORD_CONTROL_PIPE}"
    [[ -w "${RECORD_CONTROL_PIPE}" ]] || die "record control FIFO is not writable: ${RECORD_CONTROL_PIPE}"
    [[ ! -e "${RECORDING_LOCK}" ]] || die "another recording is active: ${RECORDING_LOCK}"
    mountpoint -q /mnt/data_disk || die "/mnt/data_disk is not a mount point"

    EPISODE_ROOT="$(find_episode_root "${EPISODE_ROOT_OVERRIDE}")"
    [[ -w "${EPISODE_ROOT}" ]] || die "episode root is not writable: ${EPISODE_ROOT}"

    if [[ ! -d "/sys/class/gpio/gpio${GPIO}" ]]; then
        printf '%s\n' "${GPIO}" | sudo -n tee /sys/class/gpio/export >/dev/null
        for _ in $(seq 1 20); do
            [[ -d "/sys/class/gpio/gpio${GPIO}" ]] && break
            sleep 0.05
        done
    fi
    [[ -d "/sys/class/gpio/gpio${GPIO}" ]] || die "GPIO${GPIO} did not appear after export"
    local idle_direction
    if [[ "${IDLE_LEVEL}" == "1" ]]; then
        idle_direction="high"
    else
        idle_direction="low"
    fi
    # Writing "out" makes sysfs drive the GPIO low before the following value
    # write. Configure direction and idle level atomically so an active-low
    # stopwatch cannot see a false trigger during script initialization.
    printf '%s\n' "${idle_direction}" | sudo -n tee "/sys/class/gpio/gpio${GPIO}/direction" >/dev/null
    [[ "$(<"/sys/class/gpio/gpio${GPIO}/direction")" == "out" ]] || die "GPIO${GPIO} is not configured as output"
    [[ "$(<"/sys/class/gpio/gpio${GPIO}/value")" == "${IDLE_LEVEL}" ]] || die "GPIO${GPIO} idle-level initialization failed"
    GPIO_VALUE_PATH="/sys/class/gpio/gpio${GPIO}/value"

    WORK_DIR="$(mktemp -d "/tmp/ugripper_stopwatch_record.XXXXXX")"
    BASELINE_FILE="${WORK_DIR}/baseline.txt"
    NEW_EPISODE_FILE="${WORK_DIR}/new_episode.txt"
    EPISODE_OBSERVED_FILE="${WORK_DIR}/episode_observed.txt"
    MONITOR_STOP_FILE="${WORK_DIR}/monitor.stop"
    snapshot_episode_names > "${BASELINE_FILE}"

    RECORD_START_SENT=0
    RECORD_STOP_SENT=0
    GPIO_ACTIVE=0
    RESET_HOLD_REQUIRED=0
    STOPWATCH_RESET_COMPLETED=0
    GPIO_HELPER_PID=""
    GPIO_HELPER_READ_FD=""
    GPIO_HELPER_WRITE_FD=""
    MONITOR_PID=""
    trap board_cleanup EXIT HUP INT TERM
    start_gpio_helper

    capture_time pre_reset_start
    echo "Holding GPIO${GPIO} high for ${RESET_HOLD_SEC}s to reset the stopwatch before recording"
    echo "WEB_PHASE=resetting:${RESET_HOLD_SEC}"
    board_sleep "${RESET_HOLD_SEC}"
    capture_time pre_reset_complete
    [[ "$(<"${GPIO_VALUE_PATH}")" == "${IDLE_LEVEL}" ]] || die "GPIO${GPIO} left high level during pre-record reset"

    echo "target=${TARGET_LABEL}"
    echo "episode_root=${EPISODE_ROOT}"
    echo "duration_sec=${DURATION_SEC}"
    echo "stopwatch_delay_sec=${STOPWATCH_DELAY_SEC}"
    echo "reset_hold_sec=${RESET_HOLD_SEC}"
    echo "gpio=${GPIO} active_level=${ACTIVE_LEVEL} idle_level=${IDLE_LEVEL}"

    monitor_new_episode &
    MONITOR_PID=$!

    capture_time record_start_before
    printf 'START\n' > "${RECORD_CONTROL_PIPE}"
    RECORD_START_SENT=1
    capture_time record_start_after
    local start_deadline=$((SECONDS + START_TIMEOUT_SEC))

    echo "WEB_PHASE=delay:${STOPWATCH_DELAY_SEC}"
    board_sleep "${STOPWATCH_DELAY_SEC}"

    write_gpio_precise gpio_start "${ACTIVE_LEVEL}" || die "failed to trigger stopwatch through precise GPIO helper"
    GPIO_ACTIVE=1
    RESET_HOLD_REQUIRED=1

    echo "WEB_PHASE=recording:$((DURATION_SEC - STOPWATCH_DELAY_SEC))"
    board_sleep "$((DURATION_SEC - STOPWATCH_DELAY_SEC))"

    capture_time record_stop_before
    printf 'STOP\n' > "${RECORD_CONTROL_PIPE}"
    RECORD_STOP_SENT=1
    capture_time record_stop_after
    write_gpio_precise gpio_stop "${IDLE_LEVEL}" || die "failed to stop stopwatch through precise GPIO helper"
    GPIO_ACTIVE=0
    echo "Holding GPIO${GPIO} high for ${RESET_HOLD_SEC}s to reset the stopwatch after recording"
    echo "WEB_PHASE=resetting:${RESET_HOLD_SEC}"
    board_sleep "${RESET_HOLD_SEC}"
    capture_time reset_complete
    [[ "$(<"${GPIO_VALUE_PATH}")" == "${IDLE_LEVEL}" ]] || die "GPIO${GPIO} left high level during post-stop reset"
    STOPWATCH_RESET_COMPLETED=1
    RESET_HOLD_REQUIRED=0
    stop_gpio_helper

    echo "stopwatch_start_before_unix_ns=${gpio_start_before_unix_ns}"
    echo "stopwatch_start_after_unix_ns=${gpio_start_after_unix_ns}"
    echo "stopwatch_start_window_us=$(((gpio_start_after_unix_ns - gpio_start_before_unix_ns) / 1000))"
    echo "record_start_command_unix_ns=${record_start_after_unix_ns}"
    echo "record_stop_command_unix_ns=${record_stop_after_unix_ns}"
    echo "stopwatch_stop_before_unix_ns=${gpio_stop_before_unix_ns}"
    echo "stopwatch_stop_after_unix_ns=${gpio_stop_after_unix_ns}"
    echo "stopwatch_stop_window_us=$(((gpio_stop_after_unix_ns - gpio_stop_before_unix_ns) / 1000))"
    echo "pre_record_high_hold_ms=$(((pre_reset_complete_unix_ns - pre_reset_start_unix_ns) / 1000000))"
    echo "post_stop_high_hold_ms=$(((reset_complete_unix_ns - gpio_stop_after_unix_ns) / 1000000))"

    # Keep the monitor alive until the configured START acceptance window ends.
    # A FIFO write only proves delivery to the pipe; an episode proves that the
    # runtime actually accepted the command and entered normal recording.
    if [[ ! -s "${NEW_EPISODE_FILE}" ]]; then
        while (( SECONDS < start_deadline )); do
            sleep 0.2
        done
    fi

    : > "${MONITOR_STOP_FILE}"
    wait "${MONITOR_PID}" 2>/dev/null || true
    MONITOR_PID=""

    [[ -s "${NEW_EPISODE_FILE}" ]] || die "START did not create a new episode within ${START_TIMEOUT_SEC}s; inspect ugripper.service hardware health and CONTROL_DIAG logs"
    [[ -s "${EPISODE_OBSERVED_FILE}" ]] || die "missing episode observation timestamp"
    local observed_episode
    local final_episode
    observed_episode="$(<"${NEW_EPISODE_FILE}")"
    read -r episode_observed_unix_ns episode_observed_monotonic_sec < "${EPISODE_OBSERVED_FILE}"
    echo "WEB_PHASE=finalizing:${FINALIZE_TIMEOUT_SEC}"
    final_episode="$(wait_for_final_episode "${observed_episode}")"
    write_timing_json "${final_episode}"

    [[ -s "${final_episode}/${TIMING_FILE_NAME}" ]] || die "failed to write ${TIMING_FILE_NAME}"
    [[ "$(<"/sys/class/gpio/gpio${GPIO}/value")" == "${IDLE_LEVEL}" ]] || die "GPIO${GPIO} did not return to idle"

    echo "episode=${final_episode}"
    echo "timing_file=${final_episode}/${TIMING_FILE_NAME}"
    echo "gpio_final=${IDLE_LEVEL}"
    echo "RESULT=PASS"

    trap - EXIT HUP INT TERM
    rm -rf "${WORK_DIR}"
}

host_cleanup() {
    local exit_code=$?
    trap - EXIT HUP INT TERM
    set +e
    if [[ -n "${REMOTE_SCRIPT:-}" && -n "${SSH_TARGET:-}" ]]; then
        sshpass -p "${PASSWORD}" ssh "${SSH_OPTIONS[@]}" "${SSH_TARGET}" \
            "rm -f $(printf '%q' "${REMOTE_SCRIPT}")" >/dev/null 2>&1
    fi
    exit "${exit_code}"
}

run_host() {
    HOST="${DEFAULT_HOST}"
    USER_NAME="${DEFAULT_USER}"
    PASSWORD="${DEFAULT_PASSWORD}"
    DURATION_SEC="${DEFAULT_DURATION_SEC}"
    STOPWATCH_DELAY_SEC="${DEFAULT_STOPWATCH_DELAY_SEC}"
    RESET_HOLD_SEC="${DEFAULT_RESET_HOLD_SEC}"
    GPIO="${DEFAULT_GPIO}"
    ACTIVE_LEVEL="${DEFAULT_ACTIVE_LEVEL}"
    START_TIMEOUT_SEC="${DEFAULT_START_TIMEOUT_SEC}"
    FINALIZE_TIMEOUT_SEC="${DEFAULT_FINALIZE_TIMEOUT_SEC}"
    EPISODE_ROOT_OVERRIDE=""
    CONNECT_TIMEOUT_SEC=8

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --host) HOST="$2"; shift 2 ;;
            --user) USER_NAME="$2"; shift 2 ;;
            --password) PASSWORD="$2"; shift 2 ;;
            --duration-sec) DURATION_SEC="$2"; shift 2 ;;
            --stopwatch-delay-sec) STOPWATCH_DELAY_SEC="$2"; shift 2 ;;
            --reset-hold-sec) RESET_HOLD_SEC="$2"; shift 2 ;;
            --gpio) GPIO="$2"; shift 2 ;;
            --active-level) ACTIVE_LEVEL="$2"; shift 2 ;;
            --episode-root) EPISODE_ROOT_OVERRIDE="$2"; shift 2 ;;
            --start-timeout-sec) START_TIMEOUT_SEC="$2"; shift 2 ;;
            --finalize-timeout-sec) FINALIZE_TIMEOUT_SEC="$2"; shift 2 ;;
            --connect-timeout-sec) CONNECT_TIMEOUT_SEC="$2"; shift 2 ;;
            --help|-h) usage; exit 0 ;;
            *) die "unknown argument: $1" ;;
        esac
    done

    require_positive_integer "duration-sec" "${DURATION_SEC}"
    require_nonnegative_integer "stopwatch-delay-sec" "${STOPWATCH_DELAY_SEC}"
    require_positive_integer "reset-hold-sec" "${RESET_HOLD_SEC}"
    (( RESET_HOLD_SEC >= 5 )) || die "reset-hold-sec must be at least 5 seconds"
    (( STOPWATCH_DELAY_SEC < DURATION_SEC )) || die "stopwatch-delay-sec must be less than duration-sec"
    require_nonnegative_integer "gpio" "${GPIO}"
    require_positive_integer "start-timeout-sec" "${START_TIMEOUT_SEC}"
    require_positive_integer "finalize-timeout-sec" "${FINALIZE_TIMEOUT_SEC}"
    require_positive_integer "connect-timeout-sec" "${CONNECT_TIMEOUT_SEC}"
    [[ "${ACTIVE_LEVEL}" == "0" ]] || die "active-level must be 0 for the current low-run/high-reset stopwatch"

    local command_name
    for command_name in sshpass ssh scp; do
        command -v "${command_name}" >/dev/null 2>&1 || die "missing PC dependency: ${command_name}"
    done

    SSH_TARGET="${USER_NAME}@${HOST}"
    SSH_OPTIONS=(
        -o "ConnectTimeout=${CONNECT_TIMEOUT_SEC}"
        -o StrictHostKeyChecking=accept-new
        -o ServerAliveInterval=5
        -o ServerAliveCountMax=3
    )
    REMOTE_SCRIPT="/tmp/ugripper_stopwatch_record_test_${USER_NAME}_$$.sh"
    trap host_cleanup EXIT HUP INT TERM

    echo "Copying ${SCRIPT_NAME} to ${SSH_TARGET}"
    sshpass -p "${PASSWORD}" scp "${SSH_OPTIONS[@]}" "${BASH_SOURCE[0]}" "${SSH_TARGET}:${REMOTE_SCRIPT}"

    local board_args=(
        bash "${REMOTE_SCRIPT}" --board-run
        --duration-sec "${DURATION_SEC}"
        --stopwatch-delay-sec "${STOPWATCH_DELAY_SEC}"
        --reset-hold-sec "${RESET_HOLD_SEC}"
        --gpio "${GPIO}"
        --active-level "${ACTIVE_LEVEL}"
        --start-timeout-sec "${START_TIMEOUT_SEC}"
        --finalize-timeout-sec "${FINALIZE_TIMEOUT_SEC}"
        --target-label "${SSH_TARGET}"
    )
    if [[ -n "${EPISODE_ROOT_OVERRIDE}" ]]; then
        board_args+=(--episode-root "${EPISODE_ROOT_OVERRIDE}")
    fi

    local remote_command
    remote_command="sudo -S -p '' -v >/dev/null && $(shell_join "${board_args[@]}")"

    echo "Running stopwatch recording test on ${SSH_TARGET}"
    printf '%s\n' "${PASSWORD}" | sshpass -p "${PASSWORD}" ssh "${SSH_OPTIONS[@]}" "${SSH_TARGET}" "${remote_command}"

    trap - EXIT HUP INT TERM
    sshpass -p "${PASSWORD}" ssh "${SSH_OPTIONS[@]}" "${SSH_TARGET}" \
        "rm -f $(printf '%q' "${REMOTE_SCRIPT}")" >/dev/null
    echo "Remote temporary script removed: ${REMOTE_SCRIPT}"
}

if [[ "${1:-}" == "--board-run" ]]; then
    shift
    run_board "$@"
else
    run_host "$@"
fi
