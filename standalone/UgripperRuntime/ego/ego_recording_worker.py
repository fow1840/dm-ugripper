#!/usr/bin/env python3
import argparse
import fcntl
import json
import os
import re
import shlex
import shutil
import struct
import subprocess
import sys
import time
from contextlib import contextmanager
from pathlib import Path

for _logging_dir in (
    Path(__file__).resolve().parents[3] / "scripts" / "logging",
    Path("/usr/local/lib/ugripper/logging"),
):
    if (_logging_dir / "ugripper_logger.py").is_file():
        sys.path.insert(0, str(_logging_dir))
        break
from ugripper_logger import ugripper_log


PACKAGE = "com.ssnwt.helloxr"
APP_EXTERNAL_ROOT = f"/sdcard/Android/data/{PACKAGE}/files"
LEGACY_REMOTE_ROOT = f"{APP_EXTERNAL_ROOT}/dataset"
REMOTE_ROOT_OVERRIDE_ENV = "UGRIPPER_EGO_REMOTE_ROOT"
FILES = (
    "rgb.mp4",
    "tracking.mp4",
    "ctrl.mp4",
    "audio.m4a",
    "sensor.mcap",
    "calibration.json",
    "metadata.json",
)
MP4_FILES = (
    "rgb.mp4",
    "tracking.mp4",
    "ctrl.mp4",
    "audio.m4a",
)
MP4_HEADER_SCAN_BYTES = int(os.environ.get("UGRIPPER_EGO_MP4_HEADER_SCAN_BYTES", str(1024 * 1024)))
MP4_HEADER_MERGE_GAP = int(os.environ.get("UGRIPPER_EGO_MP4_HEADER_MERGE_GAP", "4096"))
TIME_SYNC_SAMPLES = int(os.environ.get("UGRIPPER_EGO_TIME_SYNC_SAMPLES", "3"))
TIME_SYNC_MAX_OFFSET_MS = 15.0
TIME_SYNC_SET_AHEAD_MS = float(os.environ.get("UGRIPPER_EGO_TIME_SYNC_SET_AHEAD_MS", "20"))
TIME_SYNC_MAX_CORRECTIONS = min(
    3, max(0, int(os.environ.get("UGRIPPER_EGO_TIME_SYNC_MAX_CORRECTIONS", "3")))
)
STOP_BROADCAST_ATTEMPTS = 3
STOP_BROADCAST_RETRY_INTERVAL_SEC = 0.1
FINAL_SYNC_ATTEMPTS = 3
FINAL_SYNC_RETRY_INTERVAL_SEC = 0.1
SXR_PROPS = {
    "ro.product.manufacturer": "SXR",
    "ro.product.model": "SXR_1",
    "ro.product.device": "SXR_1",
    "ro.product.name": "SXR_1",
    "ro.build.product": "SXR_1",
}


def now_ms() -> int:
    return int(time.time() * 1000)


def shell_quote(value: str) -> str:
    return "'" + value.replace("'", "'\\''") + "'"


class Adb:
    def __init__(self, serial: str = ""):
        self.serial = serial
        self.bin = resolve_adb_bin()

    def command(self, *args: str) -> list[str]:
        command = [self.bin]
        if self.serial:
            command.extend(["-s", self.serial])
        command.extend(args)
        return command

    def run(self, *args: str, timeout: int = 10, check: bool = False) -> subprocess.CompletedProcess:
        return subprocess.run(
            self.command(*args),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            check=check,
        )

    def exec_out_bytes(self, remote_file: str, offset: int, count: int, timeout: int = 30) -> bytes:
        remote_q = shell_quote(remote_file)
        command = (
            f"dd if={remote_q} bs=4096 skip={offset} count={count} "
            "iflag=skip_bytes,count_bytes 2>/dev/null"
        )
        return subprocess.check_output(self.command("exec-out", "sh", "-c", command), timeout=timeout)


def resolve_adb_bin() -> str:
    configured = os.environ.get("UGRIPPER_EGO_ADB", "").strip()
    if configured:
        return configured

    bundled = Path(__file__).resolve().parent.parent / "adb" / "adb"
    if bundled.exists() and os.access(bundled, os.X_OK):
        try:
            subprocess.run(
                [str(bundled), "version"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=3,
                check=True,
            )
            return str(bundled)
        except Exception:
            pass

    return shutil.which("adb") or "adb"


def write_status(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(compact_status(payload), indent=2, sort_keys=True) + "\n", encoding="utf-8")
    tmp.replace(path)


def write_json_atomic(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    tmp.replace(path)


@contextmanager
def binding_state_lock(binding_file: Path):
    binding_file.parent.mkdir(parents=True, exist_ok=True)
    lock_file = binding_file.with_suffix(".lock")
    with lock_file.open("a", encoding="utf-8") as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(lock.fileno(), fcntl.LOCK_UN)


def compact_status(payload: dict) -> dict:
    if not isinstance(payload, dict):
        return {}
    compact = dict(payload)
    for key in (
        "detection",
        "pre_start_temp_episodes",
        "mp4_header_refresh",
        "mp4_repair",
        "stop_broadcast_error",
    ):
        compact.pop(key, None)
    if isinstance(compact.get("time_sync"), dict):
        compact["time_sync"] = compact_time_sync(compact["time_sync"])
    if isinstance(compact.get("remote_cleanup"), dict):
        cleanup = compact["remote_cleanup"]
        compact["remote_cleanup"] = {
            key: cleanup[key]
            for key in ("status", "error")
            if key in cleanup and cleanup[key] not in ("", None)
        }
    if isinstance(compact.get("finalize"), dict):
        finalize = compact["finalize"]
        compact["finalize"] = {
            key: finalize[key]
            for key in ("status", "error")
            if key in finalize and finalize[key] not in ("", None)
        }
    return compact


def compact_time_sync(detail: dict) -> dict:
    keys = (
        "status",
        "elapsed_ms",
        "threshold_ms",
        "max_corrections",
        "correction_count",
        "attempt_count",
        "set_ahead_ms",
        "first_offset_ms",
        "final_offset_ms",
        "attempts",
        "set_results",
        "error",
    )
    compact = {
        key: detail[key]
        for key in keys
        if key in detail and detail[key] not in ("", None)
    }
    if isinstance(compact.get("set_results"), list):
        compact["set_results"] = [
            summarize_set_time_result(result)
            for result in compact["set_results"]
            if isinstance(result, dict)
        ]
    return compact


def read_status(path: Path) -> dict:
    if not path.exists():
        return {}
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return {}
    return payload if isinstance(payload, dict) else {}


def ego_codec_for_camera_codec(camera_codec: str) -> str:
    codec = (camera_codec or "").strip().lower()
    if codec in ("h265", "hevc"):
        return "hevc"
    return "avc"


def set_ego_video_codec(adb: Adb, camera_codec: str) -> dict:
    ego_codec = ego_codec_for_camera_codec(camera_codec)
    detail = {
        "camera_codec": camera_codec,
        "ego_codec": ego_codec,
    }
    try:
        result = adb.run(
            "shell",
            "am",
            "broadcast",
            "-a",
            f"{PACKAGE}.SET_VIDEO_CODEC",
            "--es",
            "codec",
            ego_codec,
            timeout=10,
        )
    except Exception as exc:
        detail.update({"returncode": -1, "error": str(exc)})
        return detail
    detail.update(
        {
            "returncode": result.returncode,
            "stdout": result.stdout.strip().replace("\r", ""),
            "stderr": result.stderr.strip().replace("\r", ""),
        }
    )
    return detail


def list_devices(adb: Adb) -> list[str]:
    result = adb.run("devices", "-l", timeout=5)
    if result.returncode != 0:
        raise RuntimeError((result.stderr or result.stdout).strip() or "adb devices failed")
    devices = []
    for line in result.stdout.splitlines()[1:]:
        parts = line.split()
        if len(parts) >= 2 and parts[1] == "device":
            devices.append(parts[0])
    return devices


def get_props(adb: Adb) -> dict:
    props = {}
    for key in SXR_PROPS:
        result = adb.run("shell", "getprop", key, timeout=5)
        props[key] = result.stdout.strip().replace("\r", "") if result.returncode == 0 else ""
    return props


def is_ego_device(adb: Adb) -> tuple[bool, dict]:
    props = get_props(adb)
    props_match = all(props.get(key) == value for key, value in SXR_PROPS.items())
    package_result = adb.run("shell", "pm", "path", PACKAGE, timeout=8)
    dataset_roots = list_dataset_roots(adb)
    detail = {
        "serial": adb.serial,
        "props": props,
        "props_match": props_match,
        "package_present": package_result.returncode == 0 and bool(package_result.stdout.strip()),
        "dataset_present": bool(dataset_roots),
        "dataset_roots": dataset_roots,
    }
    return props_match, detail


def detect_ego(preferred_serial: str = "") -> tuple[Adb | None, dict]:
    root_adb = Adb()
    candidates = [preferred_serial] if preferred_serial else list_devices(root_adb)
    details = []
    for serial in candidates:
        if not serial:
            continue
        adb = Adb(serial)
        ok, detail = is_ego_device(adb)
        details.append(detail)
        if ok:
            return adb, {"selected": detail, "candidates": details}
    return None, {"selected": None, "candidates": details}


def detect_egos(preferred_serial: str = "") -> tuple[list[Adb], list[dict]]:
    root_adb = Adb()
    candidates = [preferred_serial] if preferred_serial else list_devices(root_adb)
    devices = []
    details = []
    for serial in candidates:
        if not serial:
            continue
        adb = Adb(serial)
        ok, detail = is_ego_device(adb)
        details.append(detail)
        if ok:
            devices.append(adb)
    return devices, details



FEATURE_MIN_APP_VERSION = (1, 0, 4)
ENVIRONMENT_FILE = Path("/etc/environment")


def parse_release_version(value: str) -> tuple[int, int, int] | None:
    match = re.fullmatch(r"[vV]?(\d+)\.(\d+)\.(\d+)", value.strip())
    return tuple(int(part) for part in match.groups()) if match else None


def read_environment_value(key: str) -> str:
    # Parse as data, never source the system environment file as shell code.
    try:
        value = ""
        for line in ENVIRONMENT_FILE.read_text(encoding="utf-8").splitlines():
            match = re.match(r"^\s*(?:export\s+)?" + re.escape(key) + r"\s*=\s*(.*)$", line)
            if match:
                parts = shlex.split(match.group(1), comments=True)
                value = parts[0] if len(parts) == 1 else ""
        return value
    except (OSError, ValueError, UnicodeError):
        return ""


def read_app_version(adb: Adb) -> dict:
    detail = {"version_name": "", "features_supported": False}
    try:
        result = adb.run("shell", "dumpsys", "package", PACKAGE, timeout=5)
        if result.returncode != 0:
            raise RuntimeError(result.stderr.strip() or "dumpsys package failed")
        match = re.search(r"^\s*versionName=([^\s]+)", result.stdout, re.MULTILINE)
        version_name = match.group(1) if match else ""
        version = parse_release_version(version_name)
        detail.update(version_name=version_name,
                      features_supported=version is not None and version >= FEATURE_MIN_APP_VERSION)
        if version is None:
            detail["error"] = "app version unavailable or invalid"
    except Exception as exc:
        detail["error"] = str(exc)
    return detail


def read_firmware_version(adb: Adb) -> dict:
    detail = {"version": "", "read_at_ms": now_ms()}
    try:
        result = adb.run("shell", "getprop", "ro.internal.display.id", timeout=5, check=True)
        detail["version"] = result.stdout.strip()
        if not detail["version"]:
            detail["error"] = "firmware version unavailable"
    except Exception as exc:
        detail["error"] = str(exc)
    return detail


def app_broadcast(adb: Adb, receiver: str, action: str, *extras: str) -> dict:
    detail = {"status": "failed"}
    try:
        result = adb.run("shell", "am", "broadcast", "-n", f"{PACKAGE}/.{receiver}",
                         "-a", f"{PACKAGE}.{action}", *extras, timeout=5)
        detail.update(returncode=result.returncode, stdout=result.stdout.strip(),
                      stderr=result.stderr.strip())
        completed = re.search(r'Broadcast completed: result=(-?\d+), data="(.*)"', result.stdout)
        if result.returncode != 0 or not completed or int(completed.group(1)) != -1:
            raise RuntimeError("broadcast did not return RESULT_OK with JSON data")
        payload = json.loads(completed.group(2))
        if not isinstance(payload, dict):
            raise ValueError("broadcast response is not a JSON object")
        detail.update(status="returned", response=payload)
    except Exception as exc:
        detail["error"] = str(exc)
    return detail


def configure_input_current(adb: Adb, app: dict) -> dict:
    hardware = read_environment_value("HARDWARE_VERSION")
    version = parse_release_version(hardware)
    current_ua = (1600000 if version is not None and version[:2] == (2, 5)
                  and version > (2, 5, 0) else 1100000)
    detail = {"hardware_version": hardware, "current_ua": current_ua}
    if not app.get("features_supported"):
        detail.update(status="skipped", reason="app version must be >= 1.0.4")
        return detail
    detail.update(app_broadcast(adb, "InputCurrentLimitReceiver", "SET_INPUT_CURRENT_LIMIT",
                                "--ei", "current_ua", str(current_ua)))
    response = detail.get("response", {})
    ok = (detail["status"] == "returned" and response.get("status") == "ok"
          and response.get("requested_ua") == current_ua
          and response.get("callback") == "success"
          and str(response.get("readback")) == str(current_ua)
          and response.get("matched") is True and response.get("persisted") is True
          and response.get("error") is None)
    detail["status"] = "ok" if ok else "failed"
    if not ok:
        detail.setdefault("error", "input current response did not confirm applied and persisted value")
    return detail


def run_speech(args: argparse.Namespace) -> int:
    detail = {"code": 1203, "status": "skipped"}
    try:
        binding_serial = str(read_status(args.binding_file).get("serial") or "")
        if binding_serial != args.serial:
            detail["reason"] = "binding changed before speech"
        else:
            devices, _ = detect_egos(binding_serial)
            if len(devices) != 1:
                detail["reason"] = "no unique connected ego"
            else:
                adb = devices[0]
                app = read_app_version(adb)
                detail.update(serial=adb.serial, app=app)
                if not app["features_supported"]:
                    detail["reason"] = "app version must be >= 1.0.4"
                elif str(read_status(args.binding_file).get("serial") or "") != binding_serial:
                    detail["reason"] = "binding changed during speech preparation"
                else:
                    language = read_environment_value("UGRIPPER_LANG") or os.environ.get("UGRIPPER_LANG", "zh")
                    language = "en" if language.strip().lower() == "en" else "zh"
                    detail["language"] = language
                    detail.update(app_broadcast(adb, "SpeechCommandReceiver", "SPEECH_PLAY",
                                                "--ei", "code", "1203", "--es", "language", language))
                    response = detail.get("response", {})
                    accepted = (detail["status"] == "returned"
                                and response.get("protocol") == "speech.v1"
                                and response.get("code") == 1203
                                and response.get("language") == language
                                and response.get("status") == "accepted"
                                and response.get("error") is None)
                    detail["status"] = "accepted" if accepted else "failed"
                    if not accepted:
                        detail.setdefault("error", "speech request was not accepted")
    except Exception as exc:
        detail.update(status="failed", error=str(exc))
    ugripper_log("WARN" if detail["status"] == "failed" else "INFO",
                 "speech " + json.dumps(detail, ensure_ascii=False), component="ego")
    return 1 if detail["status"] == "failed" else 0


def run_bind(args: argparse.Namespace) -> int:
    status = {"status": "error", "updated_at_ms": now_ms()}
    try:
        devices, candidates = detect_egos(args.serial)
        if len(devices) != 1:
            status["error"] = (
                "no eligible SXR ego device found"
                if not devices
                else "multiple eligible SXR ego devices found"
            )
            status["candidate_count"] = len(devices)
            write_status(args.binding_status_file, status)
            return 1

        selected = devices[0]
        selected_detail = next(
            (detail for detail in candidates if detail.get("serial") == selected.serial),
            {"serial": selected.serial},
        )
        app = read_app_version(selected)
        input_current = configure_input_current(selected, app)
        ugripper_log("WARN" if input_current["status"] == "failed" else "INFO",
                     "binding features " + json.dumps({"serial": selected.serial, "app": app,
                                                        "input_current": input_current}), component="ego")
        binding = {
            "app": app,
            "firmware": read_firmware_version(selected),
            "versions_checked_at_ms": now_ms(),
            "input_current": input_current,
            "serial": selected.serial,
            "bound_at_ms": now_ms(),
            "device": selected_detail,
        }
        with binding_state_lock(args.binding_file):
            write_json_atomic(args.binding_file, binding)
            status.update({"status": "bound", "serial": selected.serial, "updated_at_ms": now_ms()})
            write_status(args.binding_status_file, status)
        return 0
    except Exception as exc:
        status["error"] = str(exc)
        status["updated_at_ms"] = now_ms()
        write_status(args.binding_status_file, status)
        return 1


def run_unbind(args: argparse.Namespace) -> int:
    status = {"status": "unbound", "updated_at_ms": now_ms()}
    try:
        with binding_state_lock(args.binding_file):
            binding = read_status(args.binding_file)
            try:
                args.binding_file.unlink()
            except FileNotFoundError:
                pass
            try:
                status["updated_at_ms"] = now_ms()
                write_status(args.binding_status_file, status)
            except Exception:
                if binding:
                    write_json_atomic(args.binding_file, binding)
                raise
        return 0
    except Exception:
        return 1


def publish_probe_status(
    args: argparse.Namespace, expected_serial: str, status: dict
) -> bool:
    with binding_state_lock(args.binding_file):
        current_serial = str(read_status(args.binding_file).get("serial") or "")
        if current_serial != expected_serial:
            return False
        if not current_serial:
            status = {"status": "unbound", "updated_at_ms": now_ms()}
        write_status(args.binding_status_file, status)
        return bool(current_serial)


def run_probe(args: argparse.Namespace) -> int:
    status = {"status": "disconnected", "updated_at_ms": now_ms()}
    serial = ""
    try:
        with binding_state_lock(args.binding_file):
            binding = read_status(args.binding_file)
            serial = str(binding.get("serial") or "")
        if not serial:
            publish_probe_status(args, serial, status)
            return 1
        connected = serial in list_devices(Adb())
        status.update(
            {
                "status": "connected" if connected else "disconnected",
                "serial": serial,
                "updated_at_ms": now_ms(),
            }
        )
        if connected and now_ms() - binding.get("versions_checked_at_ms", 0) >= 60000:
            adb = Adb(serial)
            app = read_app_version(adb)
            firmware = read_firmware_version(adb)
            with binding_state_lock(args.binding_file):
                current = read_status(args.binding_file)
                if current.get("serial") == serial:
                    current.update(app=app, firmware=firmware, versions_checked_at_ms=now_ms())
                    write_json_atomic(args.binding_file, current)
        if not connected:
            status["error"] = "bound ego is not present in adb devices"
        published = publish_probe_status(args, serial, status)
        return 0 if published and connected else 1
    except Exception as exc:
        status["error"] = str(exc)
        status["updated_at_ms"] = now_ms()
        try:
            publish_probe_status(args, serial, status)
        except Exception:
            pass
        return 1


def configured_remote_root() -> str:
    return os.environ.get(REMOTE_ROOT_OVERRIDE_ENV, "").strip().rstrip("/")


def list_dataset_roots(adb: Adb) -> list[str]:
    configured = configured_remote_root()
    if configured:
        return [configured]

    external_root_q = shell_quote(APP_EXTERNAL_ROOT)
    fallback_pattern_q = shell_quote(f"{LEGACY_REMOTE_ROOT}/*/data")
    explicit_roots = [LEGACY_REMOTE_ROOT]
    adb_serial = str(getattr(adb, "serial", "") or "")
    if adb_serial:
        explicit_roots.extend(
            (
                f"{LEGACY_REMOTE_ROOT}/{adb_serial}/data",
                f"{APP_EXTERNAL_ROOT}/{adb_serial}/data/dataset",
            )
        )
    explicit_root_checks = "".join(
        f"if [ -d {shell_quote(root)} ]; then printf '%s\\n' {shell_quote(root)}; fi; "
        for root in explicit_roots
    )
    command = (
        f"{{ {explicit_root_checks}"
        f"find {external_root_q} -mindepth 1 -maxdepth 4 -type d "
        f"\\( -name dataset -o -path {fallback_pattern_q} \\) -print "
        "2>/dev/null; } | sort -u"
    )
    result = adb.run("shell", command, timeout=8)
    roots = [line.strip().replace("\r", "") for line in result.stdout.splitlines() if line.strip()]
    return sorted(set(roots))


def list_temp_episodes(adb: Adb, remote_root: str) -> list[str]:
    root_q = shell_quote(remote_root)
    command = f'for d in {root_q}/episode_*-temp; do [ -d "$d" ] && basename "$d"; done | sort'
    result = adb.run("shell", command, timeout=8)
    if result.returncode != 0:
        return []
    return [line.strip().replace("\r", "") for line in result.stdout.splitlines() if line.strip()]


def snapshot_temp_episodes(adb: Adb) -> dict[str, set[str]]:
    return {root: set(list_temp_episodes(adb, root)) for root in list_dataset_roots(adb)}


def find_new_temp_episodes(adb: Adb, previous: dict[str, set[str]]) -> list[tuple[str, str]]:
    candidates = []
    for root in list_dataset_roots(adb):
        existing = previous.get(root, set())
        for episode in list_temp_episodes(adb, root):
            if episode not in existing:
                candidates.append((root, episode))
    return candidates


def parse_epoch_ms(text: str) -> tuple[int | None, str]:
    value = text.strip().replace("\r", "")
    if not value.isdigit():
        return None, "invalid"
    if len(value) >= 18:
        return int(value[:19]) // 1000000, "ns"
    if len(value) >= 13:
        return int(value[:13]), "ms"
    if len(value) >= 10:
        return int(value[:10]) * 1000, "s"
    return None, "invalid"


def read_remote_epoch_ms(adb: Adb) -> tuple[int | None, dict]:
    commands = (
        ("date", "+%s%3N"),
        ("date", "+%s%N"),
        ("toybox", "date", "+%s%3N"),
        ("toybox", "date", "+%s%N"),
    )
    attempts = []
    for command in commands:
        started = now_ms()
        result = adb.run("shell", *command, timeout=3)
        finished = now_ms()
        remote_ms, precision = parse_epoch_ms(result.stdout)
        attempts.append(
            {
                "command": " ".join(command),
                "returncode": result.returncode,
                "elapsed_ms": finished - started,
                "precision": precision,
                "stdout": result.stdout.strip().replace("\r", ""),
                "stderr": result.stderr.strip().replace("\r", ""),
            }
        )
        if result.returncode == 0 and remote_ms is not None:
            return remote_ms, attempts[-1]
    return None, {"attempts": attempts}


def measure_time_offset_sample(adb: Adb) -> dict:
    host_before_ms = now_ms()
    remote_ms, read_detail = read_remote_epoch_ms(adb)
    host_after_ms = now_ms()
    sample = {
        "host_before_ms": host_before_ms,
        "host_after_ms": host_after_ms,
        "rtt_ms": host_after_ms - host_before_ms,
        "remote_ms": remote_ms,
        "read": read_detail,
    }
    if remote_ms is not None:
        host_midpoint_ms = (host_before_ms + host_after_ms) / 2.0
        sample["offset_ms"] = round(remote_ms - host_midpoint_ms, 3)
    return sample


def measure_time_offsets(adb: Adb, samples: int) -> list[dict]:
    return [measure_time_offset_sample(adb) for _ in range(max(1, samples))]


def best_offset_ms(samples: list[dict]) -> float | None:
    valid = [sample for sample in samples if isinstance(sample.get("offset_ms"), (int, float))]
    if not valid:
        return None
    best = min(valid, key=lambda item: int(item.get("rtt_ms") or 999999))
    return float(best["offset_ms"])


def set_remote_time_ms(adb: Adb, target_ms: int) -> dict:
    started = now_ms()
    result = adb.run("shell", "cmd", "alarm", "set-time", str(int(target_ms)), timeout=5)
    finished = now_ms()
    return {
        "target_ms": int(target_ms),
        "returncode": result.returncode,
        "elapsed_ms": finished - started,
        "stdout": result.stdout.strip().replace("\r", ""),
        "stderr": result.stderr.strip().replace("\r", ""),
    }


def summarize_set_time_result(result: dict) -> dict:
    detail = {
        "returncode": result.get("returncode"),
        "elapsed_ms": result.get("elapsed_ms"),
    }
    error = result.get("stderr") or result.get("stdout")
    if result.get("returncode") != 0 and error:
        detail["error"] = str(error)
    return detail


def sync_ego_time(adb: Adb) -> dict:
    started = now_ms()
    threshold_ms = max(0.0, TIME_SYNC_MAX_OFFSET_MS)
    set_ahead_ms = TIME_SYNC_SET_AHEAD_MS
    detail: dict = {
        "status": "error",
        "started_at_ms": started,
        "sample_count": max(1, TIME_SYNC_SAMPLES),
        "threshold_ms": threshold_ms,
        "set_ahead_ms": set_ahead_ms,
        "max_corrections": TIME_SYNC_MAX_CORRECTIONS,
    }
    try:
        set_results = []
        attempts = []
        first_offset = None
        final_offset = None
        for attempt_index in range(TIME_SYNC_MAX_CORRECTIONS + 1):
            target_ms = now_ms() + int(round(set_ahead_ms))
            set_result = set_remote_time_ms(adb, target_ms)
            set_results.append(set_result)
            offset = best_offset_ms(measure_time_offsets(adb, TIME_SYNC_SAMPLES))
            if attempt_index == 0:
                first_offset = offset
            attempt_detail = {
                "attempt": attempt_index + 1,
                "correction_count": attempt_index,
                "set_ahead_ms": set_ahead_ms,
                "offset_ms": offset,
                "set_result": summarize_set_time_result(set_result),
            }
            attempts.append(attempt_detail)
            final_offset = offset
            set_ok = set_result.get("returncode") == 0
            within_threshold = offset is not None and abs(offset) < threshold_ms
            if set_ok and within_threshold:
                break
            if attempt_index >= TIME_SYNC_MAX_CORRECTIONS:
                break

        final_set_ok = set_results[-1].get("returncode") == 0
        within_threshold = final_offset is not None and abs(final_offset) < threshold_ms
        finished = now_ms()
        detail.update(
            {
                "status": "ok" if final_set_ok and within_threshold else "error",
                "finished_at_ms": finished,
                "elapsed_ms": finished - started,
                "attempt_count": len(attempts),
                "correction_count": max(0, len(attempts) - 1),
                "first_offset_ms": first_offset,
                "final_offset_ms": final_offset,
                "attempts": attempts,
                "set_results": [summarize_set_time_result(result) for result in set_results],
            }
        )
        if not final_set_ok:
            result = set_results[-1]
            detail["error"] = str(
                result.get("stderr") or result.get("stdout") or f"set-time rc={result.get('returncode')}"
            )
        elif final_offset is None:
            detail["error"] = "unable to read ego time after sync"
        elif not within_threshold:
            detail["error"] = (
                f"ego time sync failed offset_ms={final_offset:.3f} "
                f"threshold_ms={threshold_ms:g}"
            )
        return detail
    except Exception as exc:
        finished = now_ms()
        detail.update(
            {
                "status": "error",
                "finished_at_ms": finished,
                "elapsed_ms": finished - started,
                "error": str(exc),
            }
        )
        return detail


def remote_path(remote_root: str, episode_name: str, file_name: str = "") -> str:
    base = f"{remote_root.rstrip('/')}/{episode_name}"
    return f"{base}/{file_name}" if file_name else base


def remote_dir_exists(adb: Adb, remote_root: str, episode_name: str) -> bool:
    result = adb.run("shell", "test", "-d", remote_path(remote_root, episode_name), timeout=8)
    return result.returncode == 0


def remote_dir_missing(adb: Adb, remote_root: str, episode_name: str) -> bool:
    result = adb.run("shell", "test", "!", "-e", remote_path(remote_root, episode_name), timeout=8)
    return result.returncode == 0


def remote_file_size(adb: Adb, remote_file: str) -> int | None:
    result = adb.run("shell", "stat", "-c", "%s", remote_file, timeout=8)
    if result.returncode != 0:
        return None
    text = result.stdout.strip().replace("\r", "")
    return int(text) if text.isdigit() else None


def adb_device_ready(adb: Adb) -> tuple[bool, str]:
    try:
        result = adb.run("get-state", timeout=5)
    except Exception as exc:
        return False, str(exc)
    state = (result.stdout or "").strip().replace("\r", "")
    if result.returncode == 0 and state == "device":
        return True, ""
    error = ((result.stderr or result.stdout) or "").strip()
    return False, error or f"adb get-state returned {result.returncode} state={state or 'unknown'}"


def sync_one_file(
    adb: Adb,
    remote_root: str,
    remote_episode: str,
    local_episode_dir: Path,
    file_name: str,
    replace_shrunk: bool = False,
) -> int:
    remote_file = remote_path(remote_root, remote_episode, file_name)
    size = remote_file_size(adb, remote_file)
    if size is None:
        return 0
    local_episode_dir.mkdir(parents=True, exist_ok=True)
    local_file = local_episode_dir / file_name
    local_size = local_file.stat().st_size if local_file.exists() else 0
    if replace_shrunk and size < local_size:
        local_file.unlink()
        local_size = 0
    if size <= local_size:
        return 0
    delta = size - local_size
    data = adb.exec_out_bytes(remote_file, local_size, delta)
    with local_file.open("ab") as output:
        output.write(data)
        output.flush()
        os.fsync(output.fileno())
    return len(data)


def sync_episode_once(
    adb: Adb,
    remote_root: str,
    remote_episode: str,
    local_episode_dir: Path,
    replace_shrunk: bool = False,
) -> dict:
    copied = {}
    for file_name in FILES:
        try:
            delta = sync_one_file(adb, remote_root, remote_episode, local_episode_dir, file_name, replace_shrunk=replace_shrunk)
        except Exception as exc:
            copied[file_name] = {"delta": 0, "error": str(exc)}
        else:
            copied[file_name] = {"delta": delta}
    return copied


def parse_mp4_boxes_from_prefix(data: bytes, total_size: int) -> list[dict]:
    boxes = []
    offset = 0
    available = len(data)
    while offset + 8 <= available and offset < total_size:
        header = data[offset : offset + 8]
        box_size, box_type = struct.unpack(">I4s", header)
        header_size = 8
        if box_size == 1:
            if offset + 16 > available:
                boxes.append({"offset": offset, "size": total_size - offset, "type": "unknown", "truncated": True})
                break
            box_size = struct.unpack(">Q", data[offset + 8 : offset + 16])[0]
            header_size = 16
        elif box_size == 0:
            box_size = total_size - offset

        box_name = box_type.decode("ascii", errors="replace")
        truncated = offset + box_size > available
        if box_size < header_size or offset + box_size > total_size:
            boxes.append({"offset": offset, "size": max(0, total_size - offset), "type": box_name, "truncated": True})
            break
        boxes.append(
            {
                "offset": offset,
                "size": box_size,
                "type": box_name,
                "header_size": header_size,
                "truncated": truncated,
            }
        )
        if truncated:
            break
        offset += box_size
    return boxes


def final_header_refresh_size(remote_prefix: bytes, remote_size: int) -> int:
    boxes = parse_mp4_boxes_from_prefix(remote_prefix, remote_size)
    for box in boxes:
        if box.get("type") == "mdat":
            return min(remote_size, int(box.get("offset") or 0) + max(16, int(box.get("header_size") or 8)))
    for box in boxes:
        if box.get("type") == "moov":
            return min(remote_size, int(box["offset"]) + int(box["size"]), len(remote_prefix))
    return min(remote_size, len(remote_prefix), 4096)


def diff_ranges_bytes(before: bytes, after: bytes) -> list[tuple[int, int]]:
    common_size = min(len(before), len(after))
    ranges = []
    current_start = None
    current_end = None
    offset = 0
    while offset < common_size:
        if before[offset] == after[offset]:
            offset += 1
            continue
        start = offset
        while offset < common_size and before[offset] != after[offset]:
            offset += 1
        end = offset
        if current_start is None:
            current_start = start
            current_end = end
        elif start <= current_end + 1:
            current_end = max(current_end, end)
        else:
            ranges.append((current_start, current_end))
            current_start = start
            current_end = end
    if current_start is not None:
        ranges.append((current_start, current_end))
    if len(after) > len(before):
        ranges.append((len(before), len(after)))
    return ranges


def merge_ranges(ranges: list[tuple[int, int]], gap: int) -> list[tuple[int, int]]:
    if not ranges:
        return []
    merged = [ranges[0]]
    for start, end in ranges[1:]:
        prev_start, prev_end = merged[-1]
        if start <= prev_end + gap:
            merged[-1] = (prev_start, max(prev_end, end))
        else:
            merged.append((start, end))
    return merged


def refresh_final_mp4_header(
    adb: Adb, remote_root: str, remote_episode: str, local_episode_dir: Path, file_name: str
) -> dict:
    detail = {"file": file_name, "status": "skipped", "reason": ""}
    remote_file = remote_path(remote_root, remote_episode, file_name)
    remote_size = remote_file_size(adb, remote_file)
    if remote_size is None:
        detail["reason"] = "remote_missing"
        return detail
    local_file = local_episode_dir / file_name
    if not local_file.exists():
        detail["reason"] = "local_missing"
        return detail

    scan_size = min(remote_size, MP4_HEADER_SCAN_BYTES)
    remote_prefix = adb.exec_out_bytes(remote_file, 0, scan_size)
    if not remote_prefix:
        detail["reason"] = "remote_prefix_empty"
        return detail

    refresh_size = final_header_refresh_size(remote_prefix, remote_size)
    if refresh_size <= 0:
        detail["reason"] = "no_header_region"
        detail["remote_size"] = remote_size
        detail["boxes"] = parse_mp4_boxes_from_prefix(remote_prefix, remote_size)
        return detail
    if refresh_size > len(remote_prefix):
        remote_prefix = adb.exec_out_bytes(remote_file, 0, refresh_size)

    with local_file.open("rb") as input_file:
        local_prefix = input_file.read(refresh_size)
    ranges = merge_ranges(diff_ranges_bytes(local_prefix, remote_prefix[:refresh_size]), MP4_HEADER_MERGE_GAP)

    detail.update(
        {
            "status": "unchanged" if not ranges else "ok",
            "remote_size": remote_size,
            "local_size": local_file.stat().st_size,
            "scan_bytes": scan_size,
            "refresh_bytes": refresh_size,
            "ranges": [{"offset": start, "size": end - start, "end": end} for start, end in ranges],
            "boxes": parse_mp4_boxes_from_prefix(remote_prefix, remote_size),
        }
    )
    if not ranges:
        return detail

    with local_file.open("r+b") as output:
        for start, end in ranges:
            output.seek(start)
            output.write(remote_prefix[start:end])
        output.flush()
        os.fsync(output.fileno())
    return detail


def refresh_final_mp4_headers(adb: Adb, remote_root: str, remote_episode: str, local_episode_dir: Path) -> dict:
    details = {}
    for file_name in MP4_FILES:
        try:
            details[file_name] = refresh_final_mp4_header(adb, remote_root, remote_episode, local_episode_dir, file_name)
        except Exception as exc:
            details[file_name] = {"file": file_name, "status": "error", "error": str(exc)}
    return details


def find_valid_box_marker(data: bytes, box_name: bytes, start: int) -> int | None:
    marker = start
    while True:
        marker = data.find(box_name, marker)
        if marker < 4:
            return None
        box_start = marker - 4
        box_size = struct.unpack(">I", data[box_start:marker])[0]
        if box_size == 1 and box_start + 16 <= len(data):
            box_size = struct.unpack(">Q", data[box_start + 8 : box_start + 16])[0]
            header_size = 16
        else:
            header_size = 8
        if box_size >= header_size and box_start + box_size <= len(data):
            return box_start
        marker += 4


def repair_mp4_mdat_largesize(path: Path) -> dict:
    detail = {"file": path.name, "status": "skipped", "reason": ""}
    if not path.exists() or path.stat().st_size < 64:
        detail["reason"] = "missing_or_too_small"
        return detail

    data = path.read_bytes()
    mdat_type = data.find(b"mdat")
    if mdat_type < 4:
        detail["reason"] = "mdat_not_found"
        return detail
    mdat_start = mdat_type - 4
    header_size = struct.unpack(">I", data[mdat_start:mdat_type])[0]
    if header_size != 1:
        detail["reason"] = "mdat_not_extended_size"
        detail["mdat_header_size"] = header_size
        return detail

    moov_start = find_valid_box_marker(data, b"moov", mdat_type + 4)
    if moov_start is None or moov_start <= mdat_start + 16:
        detail["reason"] = "moov_not_found"
        return detail

    mdat_size = moov_start - mdat_start
    old_size = struct.unpack(">Q", data[mdat_start + 8 : mdat_start + 16])[0]
    detail.update(
        {
            "status": "ok",
            "mdat_offset": mdat_start,
            "moov_offset": moov_start,
            "old_mdat_largesize": old_size,
            "new_mdat_largesize": mdat_size,
        }
    )
    if old_size == mdat_size:
        detail["status"] = "unchanged"
        return detail

    with path.open("r+b") as output:
        output.seek(mdat_start + 8)
        output.write(struct.pack(">Q", mdat_size))
        output.flush()
        os.fsync(output.fileno())
    return detail


def repair_mp4_files(local_episode_dir: Path) -> dict:
    details = {}
    for file_name in MP4_FILES:
        path = local_episode_dir / file_name
        try:
            details[file_name] = repair_mp4_mdat_largesize(path)
        except Exception as exc:
            details[file_name] = {"file": file_name, "status": "error", "error": str(exc)}
    return details


def summarize_finalize_repair(header_refresh_detail: dict, repair_detail: dict) -> dict:
    errors = []
    for group_name, details in (
        ("mp4_header_refresh", header_refresh_detail),
        ("mp4_repair", repair_detail),
    ):
        if not isinstance(details, dict):
            continue
        for file_name, detail in details.items():
            if not isinstance(detail, dict):
                continue
            if detail.get("status") == "error":
                errors.append(f"{group_name}.{file_name}: {detail.get('error') or detail.get('reason') or 'error'}")
    if errors:
        return {"status": "error", "error": "; ".join(errors)}
    return {"status": "finalized"}


def cleanup_status(status: str, remote_episode: str = "", error: str = "", **extra: object) -> dict:
    detail = {"status": status}
    if remote_episode:
        detail["remote_episode"] = remote_episode
    if error:
        detail["error"] = error
    detail.update(extra)
    return detail


def safe_remote_episode_name(name: str) -> bool:
    return bool(name) and name.startswith("episode_") and "/" not in name and ".." not in name and not name.endswith("-temp")


def verify_remote_and_local_file_sizes(
    adb: Adb, remote_root: str, remote_episode: str, local_episode_dir: Path
) -> tuple[bool, dict]:
    details = {}
    ok = True
    for file_name in FILES:
        local_file = local_episode_dir / file_name
        local_size = local_file.stat().st_size if local_file.exists() else None
        remote_size = remote_file_size(adb, remote_path(remote_root, remote_episode, file_name))
        file_ok = local_size is not None and remote_size is not None and int(local_size) == int(remote_size)
        if not file_ok:
            ok = False
        details[file_name] = {
            "ok": file_ok,
            "local_size": local_size,
            "remote_size": remote_size,
        }
    return ok, details


def recover_finalized_episode_files(
    adb: Adb,
    remote_root: str,
    remote_episode: str,
    local_episode_dir: Path,
    attempts: int = FINAL_SYNC_ATTEMPTS,
    retry_interval_sec: float = FINAL_SYNC_RETRY_INTERVAL_SEC,
) -> tuple[bool, dict, str, str]:
    history = []
    last_error = "remote/local ego file sizes do not match"
    last_error_type = "ego_pull_incomplete"
    for attempt in range(1, max(1, attempts) + 1):
        ready, ready_error = adb_device_ready(adb)
        detail = {
            "attempt": attempt,
            "device_ready": ready,
        }
        if not ready:
            detail["error"] = ready_error or "ego adb device unavailable"
            history.append(detail)
            last_error = detail["error"]
            last_error_type = "ego_disconnected"
        else:
            copied = sync_episode_once(
                adb,
                remote_root,
                remote_episode,
                local_episode_dir,
                replace_shrunk=True,
            )
            try:
                sizes_ok, size_details = verify_remote_and_local_file_sizes(
                    adb, remote_root, remote_episode, local_episode_dir
                )
            except Exception as exc:
                sizes_ok = False
                size_details = {}
                detail["verify_error"] = str(exc)
            detail.update(
                {
                    "copied": copied,
                    "files": size_details,
                    "sizes_ok": sizes_ok,
                }
            )
            history.append(detail)
            if sizes_ok:
                return True, {"status": "complete", "attempts": history}, "", ""

            still_ready, disconnect_error = adb_device_ready(adb)
            if not still_ready:
                last_error = disconnect_error or "ego disconnected while verifying finalized files"
                last_error_type = "ego_disconnected"
                detail["device_ready_after_verify"] = False
                detail["error"] = last_error
            else:
                last_error = "ego finalized files are missing or incomplete after resumable pull"
                last_error_type = "ego_pull_incomplete"

        if attempt < max(1, attempts):
            time.sleep(retry_interval_sec)

    return (
        False,
        {"status": "failed", "attempts": history, "error": last_error},
        last_error,
        last_error_type,
    )


def remove_remote_episode(adb: Adb, remote_root: str, remote_episode: str) -> tuple[bool, str]:
    if not safe_remote_episode_name(remote_episode):
        return False, f"unsafe remote episode name: {remote_episode}"
    remote_q = shell_quote(remote_path(remote_root, remote_episode))
    command = f"rm -rf {remote_q}"
    result = adb.run("shell", command, timeout=30)
    if result.returncode != 0:
        return False, (result.stderr or result.stdout).strip() or "remote rm failed"
    if not remote_dir_missing(adb, remote_root, remote_episode):
        return False, "remote episode still exists after rm"
    return True, ""


def run_cleanup(args: argparse.Namespace) -> int:
    status = read_status(args.status_file)
    status.setdefault("started_at_ms", now_ms())
    status.update({"phase": "cleanup", "updated_at_ms": now_ms()})

    remote_episode = str(status.get("remote_episode") or "")
    remote_root = str(status.get("remote_root") or "").rstrip("/")
    local_episode = str(status.get("local_episode") or "")
    serial = str(status.get("serial") or args.serial or "")

    def finish(detail: dict, ok: bool = True) -> int:
        status["remote_cleanup"] = detail
        status["updated_at_ms"] = now_ms()
        write_status(args.status_file, status)
        if not ok:
            ugripper_log(
                "ERROR",
                detail.get("error") or f"ego cleanup failed: {detail.get('status', 'unknown')}",
                component="ego",
                error_code="UGR-82-05",
            )
        return 0 if ok else 1

    if status.get("status") != "finalized":
        return finish(cleanup_status("skipped", remote_episode, "ego sync status is not finalized"))
    if not safe_remote_episode_name(remote_episode):
        return finish(cleanup_status("skipped", remote_episode, "remote episode is missing, unsafe, or still temporary"), False)
    if not remote_root:
        return finish(cleanup_status("skipped", remote_episode, "remote root missing from ego_sync.json"), False)
    local_episode_dir = Path(local_episode) if local_episode else args.episode_dir / "ego"
    if not local_episode_dir.exists():
        return finish(cleanup_status("skipped", remote_episode, f"local ego episode missing: {local_episode_dir}"), False)

    try:
        adb = Adb(serial) if serial else detect_ego(args.serial)[0]
        if adb is None:
            return finish(cleanup_status("error", remote_episode, "no SXR/SXR_1 ego device found during cleanup"), False)
        if not remote_dir_exists(adb, remote_root, remote_episode):
            return finish(cleanup_status("already_missing", remote_episode, deleted_at_ms=now_ms()))
        sizes_ok, size_details = verify_remote_and_local_file_sizes(adb, remote_root, remote_episode, local_episode_dir)
        if not sizes_ok:
            recovered, recovery_detail, recovery_error, _ = recover_finalized_episode_files(
                adb,
                remote_root,
                remote_episode,
                local_episode_dir,
            )
            status["cleanup_pull_recovery"] = recovery_detail
            if not recovered:
                return finish(
                    cleanup_status(
                        "skipped",
                        remote_episode,
                        recovery_error or "remote/local file sizes do not match",
                        files=size_details,
                    ),
                    False,
                )
            sizes_ok, size_details = verify_remote_and_local_file_sizes(
                adb, remote_root, remote_episode, local_episode_dir
            )
            if not sizes_ok:
                return finish(
                    cleanup_status(
                        "skipped",
                        remote_episode,
                        "remote/local file sizes still do not match after recovery",
                        files=size_details,
                    ),
                    False,
                )
        deleted, error = remove_remote_episode(adb, remote_root, remote_episode)
        if not deleted:
            return finish(cleanup_status("error", remote_episode, error, files=size_details), False)
        return finish(cleanup_status("deleted", remote_episode, deleted_at_ms=now_ms(), files=size_details))
    except Exception as exc:
        return finish(cleanup_status("error", remote_episode, str(exc)), False)


def final_name(temp_episode: str) -> str:
    return temp_episode[:-5] if temp_episode.endswith("-temp") else temp_episode


def run_start(args: argparse.Namespace) -> int:
    status = {
        "status": "starting",
        "phase": "start",
        "started_at_ms": now_ms(),
        "updated_at_ms": now_ms(),
        "error": "",
    }
    try:
        adb, _ = detect_ego(args.serial)
        if adb is None:
            status["status"] = "not_found"
            status["error"] = "no SXR/SXR_1 ego device found"
            write_status(args.status_file, status)
            ugripper_log("WARN", status["error"], component="ego")
            return 0

        status["serial"] = adb.serial
        status["time_sync"] = {
            "status": "running",
            "started_at_ms": now_ms(),
        }
        status["updated_at_ms"] = now_ms()
        write_status(args.status_file, status)
        status["time_sync"] = sync_ego_time(adb)
        status["updated_at_ms"] = now_ms()
        write_status(args.status_file, status)
        if status["time_sync"].get("status") != "ok":
            final_offset = status["time_sync"].get("final_offset_ms")
            offset_text = f"{float(final_offset):.3f}" if isinstance(final_offset, (int, float)) else "unknown"
            status.update(
                {
                    "status": "time_sync_failed",
                    "error_type": "ego_time_sync_failed",
                    "error": (
                        f"ego time sync failed offset_ms={offset_text} "
                        f"threshold_ms={TIME_SYNC_MAX_OFFSET_MS:g}"
                    ),
                    "updated_at_ms": now_ms(),
                }
            )
            ugripper_log("ERROR", status["error"], component="ego", error_code="UGR-82-02")
            write_status(args.status_file, status)
            return 1

        existing_temp_episodes = snapshot_temp_episodes(adb)
        status["remote_roots"] = sorted(existing_temp_episodes)
        status["video_codec"] = set_ego_video_codec(adb, args.codec)
        status["updated_at_ms"] = now_ms()
        write_status(args.status_file, status)

        result = adb.run("shell", "am", "broadcast", "-a", f"{PACKAGE}.START_RECORDING", timeout=10)
        status["start_broadcast_returncode"] = result.returncode
        status["updated_at_ms"] = now_ms()
        write_status(args.status_file, status)
        if result.returncode != 0:
            status["status"] = "start_failed"
            status["error"] = (result.stderr or result.stdout).strip()
            write_status(args.status_file, status)
            ugripper_log("ERROR", status["error"], component="ego", error_code="UGR-82-03")
            return 0

        remote_root = ""
        remote_episode = ""
        deadline = time.monotonic() + args.detect_timeout
        while time.monotonic() < deadline:
            candidates = find_new_temp_episodes(adb, existing_temp_episodes)
            if len(candidates) == 1:
                remote_root, remote_episode = candidates[0]
                break
            if len(candidates) > 1:
                status.update(
                    {
                        "status": "start_ambiguous_remote_episode",
                        "error": "multiple new ego episode_*-temp directories found",
                        "remote_episode_candidates": [
                            {"remote_root": root, "remote_episode": episode} for root, episode in candidates
                        ],
                        "updated_at_ms": now_ms(),
                    }
                )
                write_status(args.status_file, status)
                ugripper_log("ERROR", status["error"], component="ego", error_code="UGR-82-03")
                return 0
            time.sleep(args.interval)

        if not remote_episode:
            status["status"] = "start_timeout"
            status["error"] = "ego app did not create episode_*-temp"
            write_status(args.status_file, status)
            ugripper_log("ERROR", status["error"], component="ego", error_code="UGR-82-03")
            return 0

        status.update(
            {
                "status": "recording",
                "phase": "sync",
                "remote_root": remote_root,
                "remote_episode": remote_episode,
                "local_episode": str(args.episode_dir / "ego"),
            }
        )
        write_status(args.status_file, status)

        local_episode_dir = args.episode_dir / "ego"
        while True:
            sync_episode_once(adb, remote_root, remote_episode, local_episode_dir)
            status["updated_at_ms"] = now_ms()
            write_status(args.status_file, status)
            time.sleep(args.interval)
    except KeyboardInterrupt:
        return 0
    except Exception as exc:
        status["status"] = "error"
        status["error"] = str(exc)
        status["updated_at_ms"] = now_ms()
        write_status(args.status_file, status)
        ugripper_log("ERROR", status["error"], component="ego", error_code="UGR-82-04")
        return 0


def run_stop(args: argparse.Namespace) -> int:
    status = read_status(args.status_file)
    status.setdefault("started_at_ms", now_ms())
    status.update({"phase": "stop", "updated_at_ms": now_ms()})

    serial = str(status.get("serial") or args.serial or "")
    remote_root = str(status.get("remote_root") or "").rstrip("/")
    remote_episode = str(status.get("remote_episode") or "")
    recording_started = bool(remote_episode)
    adb = Adb(serial) if serial else None
    stop_attempts = []
    stop_succeeded = False
    last_failure_status = "not_found"
    last_error = "no SXR/SXR_1 ego device found during stop"
    for attempt in range(1, STOP_BROADCAST_ATTEMPTS + 1):
        detail = {"attempt": attempt, "started_at_ms": now_ms()}
        try:
            if adb is None:
                adb, _ = detect_ego(args.serial)
            if adb is None:
                detail.update({"status": "device_unavailable", "error": last_error})
                last_failure_status = "device_disconnected"
            else:
                ready, ready_error = adb_device_ready(adb)
                if not ready:
                    last_error = ready_error or "ego adb device unavailable during stop"
                    detail.update({"status": "device_unavailable", "error": last_error})
                    last_failure_status = "device_disconnected"
                else:
                    result = adb.run(
                        "shell",
                        "am",
                        "broadcast",
                        "-a",
                        f"{PACKAGE}.STOP_RECORDING",
                        timeout=10,
                    )
                    detail.update(
                        {
                            "returncode": result.returncode,
                            "stdout": (result.stdout or "").strip(),
                            "stderr": (result.stderr or "").strip(),
                        }
                    )
                    if result.returncode == 0:
                        detail["status"] = "ok"
                        stop_succeeded = True
                    else:
                        last_error = ((result.stderr or result.stdout) or "").strip() or (
                            f"ego STOP_RECORDING broadcast returned {result.returncode}"
                        )
                        detail.update({"status": "broadcast_failed", "error": last_error})
                        last_failure_status = "stop_failed"
        except Exception as exc:
            last_error = str(exc)
            detail.update({"status": "exception", "error": last_error})
            last_failure_status = "device_disconnected"
        detail["finished_at_ms"] = now_ms()
        stop_attempts.append(detail)
        status["stop_attempts"] = stop_attempts
        status["updated_at_ms"] = now_ms()
        write_status(args.status_file, status)
        if stop_succeeded:
            break
        if attempt < STOP_BROADCAST_ATTEMPTS:
            time.sleep(STOP_BROADCAST_RETRY_INTERVAL_SEC)

    if not stop_succeeded:
        status["status"] = last_failure_status if recording_started else "not_found"
        status["error"] = last_error
        if recording_started:
            status["error_type"] = "ego_disconnected"
        status["updated_at_ms"] = now_ms()
        write_status(args.status_file, status)
        if recording_started:
            ugripper_log("ERROR", status["error"], component="ego", error_code="UGR-82-01")
            return 1
        ugripper_log("WARN", status["error"], component="ego")
        return 0

    if not remote_episode:
        status["status"] = "stop_no_remote_episode"
        status["error"] = "remote_episode missing from ego_sync.json"
        status["updated_at_ms"] = now_ms()
        write_status(args.status_file, status)
        ugripper_log("ERROR", status["error"], component="ego", error_code="UGR-82-05")
        return 0
    if not remote_root:
        status["status"] = "stop_no_remote_root"
        status["error"] = "remote_root missing from ego_sync.json"
        status["updated_at_ms"] = now_ms()
        write_status(args.status_file, status)
        ugripper_log("ERROR", status["error"], component="ego", error_code="UGR-82-05")
        return 0

    local_episode_dir = args.episode_dir / "ego"
    final_episode = final_name(remote_episode)
    final_local_dir = args.episode_dir / "ego"
    deadline = time.monotonic() + args.finalize_timeout
    while time.monotonic() < deadline:
        try:
            temp_exists = remote_dir_exists(adb, remote_root, remote_episode)
            final_exists = remote_dir_exists(adb, remote_root, final_episode)
        except Exception as exc:
            status.update(
                {
                    "status": "device_disconnected",
                    "error_type": "ego_disconnected",
                    "error": str(exc),
                    "updated_at_ms": now_ms(),
                }
            )
            write_status(args.status_file, status)
            ugripper_log("ERROR", status["error"], component="ego", error_code="UGR-82-01")
            return 1
        if temp_exists:
            current_remote = remote_episode
            current_exists = True
        elif final_exists:
            current_remote = final_episode
            current_exists = True
        else:
            status["updated_at_ms"] = now_ms()
            write_status(args.status_file, status)
            time.sleep(args.interval)
            continue
        if current_exists:
            sync_episode_once(
                adb,
                remote_root,
                current_remote,
                local_episode_dir,
                replace_shrunk=(current_remote == final_episode),
            )
        if current_remote == final_episode:
            recovered, recovery_detail, recovery_error, recovery_error_type = recover_finalized_episode_files(
                adb,
                remote_root,
                final_episode,
                final_local_dir,
            )
            status["pull_recovery"] = recovery_detail
            if not recovered:
                status.update(
                    {
                        "status": "device_disconnected"
                        if recovery_error_type == "ego_disconnected"
                        else "pull_incomplete",
                        "phase": "done",
                        "remote_episode": final_episode,
                        "local_episode": str(final_local_dir),
                        "error_type": recovery_error_type,
                        "error": recovery_error,
                        "updated_at_ms": now_ms(),
                    }
                )
                write_status(args.status_file, status)
                ugripper_log(
                    "ERROR",
                    status["error"],
                    component="ego",
                    error_code="UGR-82-01" if recovery_error_type == "ego_disconnected" else "UGR-82-04",
                )
                return 1
            header_refresh_detail = refresh_final_mp4_headers(adb, remote_root, final_episode, final_local_dir)
            repair_detail = repair_mp4_files(final_local_dir)
            status.update(
                {
                    "status": "finalized",
                    "phase": "done",
                    "remote_episode": final_episode,
                    "local_episode": str(final_local_dir),
                    "finalize": summarize_finalize_repair(header_refresh_detail, repair_detail),
                    "updated_at_ms": now_ms(),
                }
            )
            status.pop("error_type", None)
            status["error"] = ""
            write_status(args.status_file, status)
            return 0
        status["updated_at_ms"] = now_ms()
        write_status(args.status_file, status)
        time.sleep(args.interval)

    status["status"] = "finalize_timeout"
    status["error_type"] = "ego_pull_incomplete"
    status["error"] = "ego episode did not finalize before timeout"
    status["updated_at_ms"] = now_ms()
    write_status(args.status_file, status)
    ugripper_log("ERROR", status["error"], component="ego", error_code="UGR-82-05")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("start", "stop", "cleanup", "bind", "unbind", "probe", "speech"))
    parser.add_argument("--episode-dir", type=Path)
    parser.add_argument("--status-file", type=Path)
    parser.add_argument("--binding-file", type=Path)
    parser.add_argument("--binding-status-file", type=Path)
    parser.add_argument("--serial", default=os.environ.get("UGRIPPER_EGO_SERIAL", ""))
    parser.add_argument("--interval", type=float, default=float(os.environ.get("UGRIPPER_EGO_SYNC_INTERVAL_SEC", "1")))
    parser.add_argument("--detect-timeout", type=float, default=15.0)
    parser.add_argument("--finalize-timeout", type=float, default=60.0)
    parser.add_argument("--codec", choices=("h264", "h265"), default="h264")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.command == "speech":
        if args.binding_file is None:
            raise SystemExit("--binding-file is required")
        return run_speech(args)
    if args.command in ("bind", "unbind", "probe"):
        if args.binding_file is None or args.binding_status_file is None:
            raise SystemExit("--binding-file and --binding-status-file are required")
        if args.command == "bind":
            return run_bind(args)
        if args.command == "unbind":
            return run_unbind(args)
        return run_probe(args)
    if args.episode_dir is None or args.status_file is None:
        raise SystemExit("--episode-dir and --status-file are required")
    args.episode_dir.mkdir(parents=True, exist_ok=True)
    if args.command == "start":
        return run_start(args)
    if args.command == "stop":
        return run_stop(args)
    return run_cleanup(args)


if __name__ == "__main__":
    sys.exit(main())
