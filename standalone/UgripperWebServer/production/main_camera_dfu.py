#!/usr/bin/env python3
"""Safe, testable main-camera DFU orchestration.

The module deliberately contains no desktop or packaging integration.  All
hardware-facing operations are behind injectable callables so the state
machine can be tested without a camera connected.
"""

from __future__ import annotations

import hashlib
import os
import re
import shutil
import subprocess
import time
from dataclasses import asdict, dataclass
from enum import Enum
from pathlib import Path
from typing import Any, Callable, Iterable, Mapping, Sequence


DEFAULT_FIRMWARE_DIR = Path(
    os.environ.get(
        "UGRIPPER_MAIN_CAMERA_FIRMWARE_DIR",
        str(Path(__file__).resolve().parents[1] / "main_camera_firmware"),
    )
)
DFU_VID = "0955"
DFU_PID = "701a"
DFU_DEVICE_ID = f"{DFU_VID}:{DFU_PID}"
UVC_VID = "27c2"
UVC_PID = "0530"
UVC_DEVICE_ID = f"{UVC_VID}:{UVC_PID}"
CAMERA_DEVICE_BY_SIDE = {"left": "/dev/cam_left", "right": "/dev/cam_right"}
USB_PORT_RE = re.compile(r"^\d+-\d+(?:\.\d+)*$")
VERSION_VALUE_PATTERN = r"(?:\d{4}-\d{2}-\d{2}(?:-\d{3,6})?|\d+(?:\.\d+){1,5})"
VERSION_RE = re.compile(rf"(?<!\d)(?:V(?:ersion)?\s*[:=]?\s*)?({VERSION_VALUE_PATTERN})(?!\d)", re.I)
IMAGE_VERSION_LABEL_RE = re.compile(
    rf"(?:待升级|目标|镜像|image|target)(?:\s+firmware)?\s*(?:固件)?\s*(?:版本|version)"
    rf"[^\r\n:：]*[:：=]\s*(?P<version>{VERSION_VALUE_PATTERN})",
    re.I,
)
CURRENT_VERSION_LABEL_RE = re.compile(
    rf"(?:当前|current)(?:\s+UVC)?(?:\s+摄像头|\s+camera)?(?:\s+firmware)?\s*(?:版本|version)"
    rf"[^\r\n:：]*[:：=]\s*(?P<version>{VERSION_VALUE_PATTERN})",
    re.I,
)


class DfuState(str, Enum):
    VALIDATING = "validating"
    PREREQUISITES = "prerequisites"
    READING_VERSION = "reading_version"
    LOCATING_UVC = "locating_uvc"
    ENTERING_DFU = "entering_dfu"
    WAITING_FOR_DFU = "waiting_for_dfu"
    FLASHING = "flashing"
    WAITING_FOR_UVC = "waiting_for_uvc"
    VERIFYING_VERSION = "verifying_version"
    REGRESSION_CHECK = "regression_check"
    COMPLETE = "complete"
    FAILED = "failed"


class DfuError(RuntimeError):
    """An error safe to serialize into a Web UI task result."""

    def __init__(
        self,
        code: str,
        message: str,
        *,
        state: DfuState | str | None = None,
        details: Mapping[str, Any] | None = None,
    ) -> None:
        super().__init__(message)
        self.code = code
        self.message = message
        self.state = state.value if isinstance(state, DfuState) else state
        self.details = dict(details or {})

    def to_dict(self) -> dict[str, Any]:
        return {
            "type": "error",
            "code": self.code,
            "message": self.message,
            "state": self.state,
            "details": self.details,
        }


class PrerequisiteError(DfuError):
    def __init__(self, code: str, message: str, **kwargs: Any) -> None:
        super().__init__(code, message, state=DfuState.PREREQUISITES, **kwargs)


@dataclass(frozen=True)
class FirmwareImage:
    path: str
    name: str
    size: int
    sha256: str
    version: str

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass(frozen=True)
class DfuDevice:
    port: str
    vendor: str = DFU_VID
    product: str = DFU_PID
    alt: int | None = None
    description: str = ""

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass(frozen=True)
class DfuResult:
    side: str
    usb_port: str
    image: FirmwareImage
    previous_version: str
    installed_version: str
    regression: Mapping[str, Any]
    command: tuple[str, ...]
    skipped: bool = False
    skip_reason: str = ""

    def to_dict(self) -> dict[str, Any]:
        result = asdict(self)
        result["image"] = self.image.to_dict()
        result["command"] = list(self.command)
        return result


EventSink = Callable[[dict[str, Any]], None]
Runner = Callable[..., subprocess.CompletedProcess[str]]


def _emit(sink: EventSink | None, event_type: str, **payload: Any) -> None:
    if sink:
        sink({"type": event_type, **payload})


def _state(sink: EventSink | None, value: DfuState, message: str) -> None:
    _emit(sink, "state", state=value.value, message=message)


def _resolved_directory(directory: Path | str) -> Path:
    path = Path(directory).expanduser()
    try:
        resolved = path.resolve(strict=True)
    except OSError as exc:
        raise DfuError(
            "firmware_directory_missing",
            f"主摄固件目录不存在：{path}",
            state=DfuState.VALIDATING,
            details={"directory": str(path)},
        ) from exc
    if not resolved.is_dir():
        raise DfuError(
            "firmware_directory_invalid",
            f"主摄固件路径不是目录：{resolved}",
            state=DfuState.VALIDATING,
        )
    return resolved


def validate_firmware_path(
    image_path: Path | str,
    firmware_dir: Path | str = DEFAULT_FIRMWARE_DIR,
) -> Path:
    """Resolve an image and enforce the persistent-directory allow-list."""

    allowed = _resolved_directory(firmware_dir)
    requested = Path(image_path).expanduser()
    if not requested.is_absolute():
        requested = allowed / requested
    try:
        image = requested.resolve(strict=True)
    except OSError as exc:
        raise DfuError(
            "firmware_image_missing",
            f"主摄固件不存在：{requested}",
            state=DfuState.VALIDATING,
        ) from exc
    try:
        image.relative_to(allowed)
    except ValueError as exc:
        raise DfuError(
            "firmware_not_allowed",
            "只能选择主摄固件目录中的镜像",
            state=DfuState.VALIDATING,
            details={"image": str(image), "allowed_directory": str(allowed)},
        ) from exc
    if not image.is_file() or image.suffix.lower() != ".img":
        raise DfuError(
            "firmware_image_invalid",
            "主摄固件必须是普通 .img 文件",
            state=DfuState.VALIDATING,
            details={"image": str(image)},
        )
    return image


def sha256_file(path: Path, chunk_size: int = 1024 * 1024) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(chunk_size), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _fallback_version(output: str) -> str:
    candidates = VERSION_RE.findall(output or "")
    if not candidates:
        raise DfuError(
            "firmware_version_unparseable",
            "无法从 linux_read_version 输出解析固件版本",
            state=DfuState.READING_VERSION,
            details={"output": (output or "")[-1000:]},
        )
    return candidates[0]


def parse_image_version(output: str) -> str:
    """Read the target image version, even when output also has UVC version."""

    match = IMAGE_VERSION_LABEL_RE.search(output or "")
    return match.group("version") if match else _fallback_version(output)


def parse_current_camera_version(output: str) -> str:
    """Read current UVC version without accidentally returning image version."""

    match = CURRENT_VERSION_LABEL_RE.search(output or "")
    if match:
        return match.group("version")
    candidates = VERSION_RE.findall(output or "")
    if not candidates:
        return _fallback_version(output)
    return candidates[-1]


def parse_version_output(output: str) -> str:
    """Backward-compatible alias for single-version image output."""

    return parse_image_version(output)


def find_version_reader(explicit: Path | str | None = None) -> str:
    candidates: list[str] = []
    if explicit:
        candidates.append(str(explicit))
    configured = os.environ.get("UGRIPPER_LINUX_READ_VERSION", "").strip()
    if configured:
        candidates.append(configured)
    candidates.append(
        str(Path(__file__).resolve().parents[1] / "portable_runtime/bin/CameraRecorder/linux_read_version")
    )
    found = shutil.which("linux_read_version")
    if found:
        candidates.append(found)
    for value in candidates:
        path = Path(value).expanduser()
        if path.is_file() and os.access(path, os.X_OK):
            return str(path.resolve())
    raise PrerequisiteError(
        "version_reader_missing",
        "缺少 ARM64 linux_read_version，无法读取主摄镜像/设备版本",
        details={"environment": "UGRIPPER_LINUX_READ_VERSION"},
    )


def read_image_version(
    image: Path,
    *,
    version_reader: Path | str | None = None,
    device_path: str | None = None,
    runner: Runner = subprocess.run,
) -> str:
    reader = find_version_reader(version_reader)
    explicit_command = [reader, "--image", str(image)]
    if device_path:
        explicit_command.extend(("--device", device_path))
    result = runner(
        explicit_command,
        capture_output=True,
        text=True,
        timeout=15,
        check=False,
    )
    attempts = [{"command": explicit_command, "returncode": result.returncode,
                 "stdout": result.stdout[-2000:], "stderr": result.stderr[-2000:]}]
    output = "\n".join((result.stdout, result.stderr))
    # Runtime/device/image failures are not evidence of an old CLI. A tool may
    # print the image version successfully before failing to open the camera.
    cli_rejected = result.returncode == 2 and not IMAGE_VERSION_LABEL_RE.search(output) and (
        re.search(r"(?:unknown|unrecognized|unrecognised|invalid|unsupported)\s+(?:option|argument)", output, re.I)
        or re.search(r"^\s*usage:", output, re.I | re.M)
    )
    if cli_rejected:
        fallback_command = [reader, str(image)]
        if device_path:
            fallback_command.extend(("--device", device_path))
        result = runner(
            fallback_command,
            capture_output=True,
            text=True,
            timeout=15,
            check=False,
        )
        attempts.append({"command": fallback_command, "returncode": result.returncode,
                         "stdout": result.stdout[-2000:], "stderr": result.stderr[-2000:]})
    if result.returncode != 0:
        output = "\n".join((result.stdout, result.stderr))
        device_failed = bool(device_path and IMAGE_VERSION_LABEL_RE.search(output))
        cause = result.stderr.strip()[-2000:] or result.stdout.strip()[-2000:]
        message = (f"读取主摄设备版本失败：{device_path}" if device_failed
                   else "linux_read_version 读取镜像版本失败")
        raise DfuError(
            "device_version_read_failed" if device_failed else "firmware_version_read_failed",
            message + (f"；{cause}" if cause else ""),
            state=DfuState.READING_VERSION,
            details={"returncode": result.returncode, "stdout": result.stdout[-2000:],
                     "stderr": result.stderr[-2000:], "attempts": attempts},
        )
    return parse_image_version("\n".join((result.stdout, result.stderr)))


def inspect_firmware(
    image_path: Path | str,
    *,
    firmware_dir: Path | str = DEFAULT_FIRMWARE_DIR,
    version_reader: Path | str | None = None,
    device_path: str | None = None,
    runner: Runner = subprocess.run,
) -> FirmwareImage:
    image = validate_firmware_path(image_path, firmware_dir)
    return FirmwareImage(
        path=str(image),
        name=image.name,
        size=image.stat().st_size,
        sha256=sha256_file(image),
        version=read_image_version(
            image, version_reader=version_reader, device_path=device_path, runner=runner
        ),
    )


def _usb_device_ancestor(path: Path) -> Path:
    for candidate in (path, *path.parents):
        if (candidate / "idVendor").is_file() and (candidate / "idProduct").is_file():
            if USB_PORT_RE.fullmatch(candidate.name):
                return candidate
    raise DfuError(
        "uvc_usb_port_not_found",
        f"无法从 sysfs 定位主摄 USB 物理端口：{path}",
        state=DfuState.LOCATING_UVC,
    )


def uvc_side_to_usb_port(
    side: str,
    *,
    device_by_side: Mapping[str, str] = CAMERA_DEVICE_BY_SIDE,
    sys_class_video: Path | str = "/sys/class/video4linux",
    realpath: Callable[[str], str] = os.path.realpath,
) -> str:
    if side not in device_by_side:
        raise DfuError(
            "invalid_camera_side",
            f"未知主摄侧：{side}",
            state=DfuState.LOCATING_UVC,
            details={"allowed": sorted(device_by_side)},
        )
    device = device_by_side[side]
    video_name = Path(realpath(device)).name
    class_node = Path(sys_class_video) / video_name
    try:
        device_node = class_node.resolve(strict=True)
    except OSError as exc:
        raise DfuError(
            "uvc_device_missing",
            f"{side} 主摄未枚举：{device}",
            state=DfuState.LOCATING_UVC,
            details={"device": device, "sysfs": str(class_node)},
        ) from exc
    usb_device = _usb_device_ancestor(device_node)
    try:
        vendor = (usb_device / "idVendor").read_text(encoding="ascii").strip().lower()
        product = (usb_device / "idProduct").read_text(encoding="ascii").strip().lower()
    except OSError as exc:
        raise DfuError(
            "uvc_usb_identity_unreadable",
            f"无法读取 {side} 主摄 USB 身份：{usb_device}",
            state=DfuState.LOCATING_UVC,
            details={"device": device, "usb_device": str(usb_device)},
        ) from exc
    if (vendor, product) != (UVC_VID, UVC_PID):
        raise DfuError(
            "uvc_usb_identity_mismatch",
            f"{side} 主摄不是预期的 USB 设备 {UVC_DEVICE_ID}",
            state=DfuState.LOCATING_UVC,
            details={
                "device": device,
                "usb_port": usb_device.name,
                "expected": UVC_DEVICE_ID,
                "actual": f"{vendor}:{product}",
            },
        )
    return usb_device.name


DFU_LIST_RE = re.compile(
    r"Found\s+DFU:\s*\[(?P<vid>[0-9a-f]{4}):(?P<pid>[0-9a-f]{4})\](?P<body>.*)",
    re.I,
)
DFU_PATH_RE = re.compile(r'\bpath="(?P<path>\d+-\d+(?:\.\d+)*)"', re.I)
DFU_ALT_RE = re.compile(r"\balt=(?P<alt>\d+)", re.I)
DFU_NAME_RE = re.compile(r'\bname="(?P<name>[^"]*)"', re.I)


def parse_dfu_util_list(output: str) -> list[DfuDevice]:
    devices: list[DfuDevice] = []
    for line in (output or "").splitlines():
        match = DFU_LIST_RE.search(line)
        if not match:
            continue
        if match.group("vid").lower() != DFU_VID or match.group("pid").lower() != DFU_PID:
            continue
        body = match.group("body")
        path_match = DFU_PATH_RE.search(body)
        if not path_match:
            continue
        alt_match = DFU_ALT_RE.search(body)
        name_match = DFU_NAME_RE.search(body)
        devices.append(
            DfuDevice(
                port=path_match.group("path"),
                alt=int(alt_match.group("alt")) if alt_match else None,
                description=name_match.group("name") if name_match else "",
            )
        )
    return devices


def enumerate_dfu_sysfs(sys_usb_devices: Path | str = "/sys/bus/usb/devices") -> list[DfuDevice]:
    devices: list[DfuDevice] = []
    for node in sorted(Path(sys_usb_devices).glob("*")):
        if not USB_PORT_RE.fullmatch(node.name):
            continue
        try:
            vendor = (node / "idVendor").read_text(encoding="ascii").strip().lower()
            product = (node / "idProduct").read_text(encoding="ascii").strip().lower()
        except OSError:
            continue
        if (vendor, product) == (DFU_VID, DFU_PID):
            devices.append(DfuDevice(port=node.name))
    return devices


def build_dfu_command(dfu_util: str, port: str, image: Path | str) -> list[str]:
    if not USB_PORT_RE.fullmatch(port):
        raise DfuError(
            "invalid_usb_port",
            f"非法 USB 物理端口：{port}",
            state=DfuState.FLASHING,
        )
    return [
        dfu_util,
        "-d",
        DFU_DEVICE_ID,
        "-p",
        port,
        "-a",
        "0",
        "-D",
        str(image),
        "-R",
    ]


def find_dfu_util(explicit: Path | str | None = None) -> str:
    value = str(explicit) if explicit else shutil.which("dfu-util")
    if value and Path(value).is_file() and os.access(value, os.X_OK):
        return str(Path(value).resolve())
    raise PrerequisiteError("dfu_util_missing", "缺少 dfu-util，无法升级主摄固件")


def find_dfu_trigger(explicit: Path | str | None = None) -> str:
    candidates = []
    if explicit:
        candidates.append(str(explicit))
    configured = os.environ.get("UGRIPPER_MAIN_CAMERA_DFU_TRIGGER", "").strip()
    if configured:
        candidates.append(configured)
    candidates.append(
        str(Path(__file__).resolve().parents[1] / "portable_runtime/bin/CameraRecorder/main_camera_dfu_trigger")
    )
    found = shutil.which("main_camera_dfu_trigger")
    if found:
        candidates.append(found)
    for value in candidates:
        path = Path(value).expanduser()
        if path.is_file() and os.access(path, os.X_OK):
            return str(path.resolve())
    raise PrerequisiteError(
        "dfu_trigger_missing",
        "缺少主摄从 UVC 进入 DFU 的厂商工具",
        details={"environment": "UGRIPPER_MAIN_CAMERA_DFU_TRIGGER"},
    )


def versions_equal(expected: str, actual: str) -> bool:
    def key(value: str) -> tuple[int, ...]:
        return tuple(int(part) for part in re.findall(r"\d+", value))

    return bool(key(expected)) and key(expected) == key(actual)


def assert_firmware_unchanged(
    image: FirmwareImage,
    image_path: Path,
    *,
    code: str = "firmware_changed_before_flash",
    state: DfuState = DfuState.FLASHING,
) -> None:
    actual_size = image_path.stat().st_size
    actual_hash = sha256_file(image_path)
    if actual_size != image.size or actual_hash != image.sha256:
        raise DfuError(
            code,
            "主摄固件在版本检查后发生变化，拒绝进入 DFU 或写入",
            state=state,
            details={
                "expected_size": image.size,
                "actual_size": actual_size,
                "expected_sha256": image.sha256,
                "actual_sha256": actual_hash,
            },
        )


class MainCameraDfuUpdater:
    """One-camera-at-a-time DFU state machine bound to a physical USB port."""

    def __init__(
        self,
        *,
        firmware_dir: Path | str = DEFAULT_FIRMWARE_DIR,
        version_reader: Path | str | None = None,
        dfu_trigger: Path | str | None = None,
        dfu_util: Path | str | None = None,
        runner: Runner = subprocess.run,
        event_sink: EventSink | None = None,
        clock: Callable[[], float] = time.monotonic,
        sleeper: Callable[[float], None] = time.sleep,
        locate_uvc_port: Callable[[str], str] = uvc_side_to_usb_port,
        enumerate_dfu: Callable[[], Iterable[DfuDevice]] = enumerate_dfu_sysfs,
        camera_present: Callable[[str], bool] | None = None,
        read_device_version: Callable[[str], str] | None = None,
        regression_check: Callable[[str], Mapping[str, Any]] | None = None,
        cancel_check: Callable[[], None] | None = None,
        poll_interval: float = 0.25,
    ) -> None:
        self.firmware_dir = Path(firmware_dir)
        self.version_reader = version_reader
        self.dfu_trigger = dfu_trigger
        self.dfu_util = dfu_util
        self.runner = runner
        self.event_sink = event_sink
        self.clock = clock
        self.sleeper = sleeper
        self.locate_uvc_port = locate_uvc_port
        self.enumerate_dfu = enumerate_dfu
        self.camera_present = camera_present or (
            lambda side: os.path.exists(CAMERA_DEVICE_BY_SIDE[side])
        )
        self.read_device_version = read_device_version
        self.regression_check = regression_check
        self.cancel_check = cancel_check or (lambda: None)
        self.poll_interval = poll_interval
        self.state: DfuState | None = None

    def _transition(self, value: DfuState, message: str) -> None:
        self.state = value
        _state(self.event_sink, value, message)
        self.cancel_check()

    def _run(
        self,
        command: Sequence[str],
        *,
        timeout: float,
        code: str,
        state: DfuState,
        allow_nonzero_with_markers: Sequence[str] = (),
    ) -> subprocess.CompletedProcess[str]:
        self.cancel_check()
        _emit(self.event_sink, "log", stream="command", message=" ".join(command))
        try:
            result = self.runner(
                list(command), capture_output=True, text=True, timeout=timeout, check=False
            )
        except subprocess.TimeoutExpired as exc:
            raise DfuError(
                f"{code}_timeout",
                f"命令执行超时：{command[0]}",
                state=state,
                details={"timeout": timeout},
            ) from exc
        except OSError as exc:
            raise DfuError(
                code,
                f"命令无法启动：{command[0]}",
                state=state,
                details={"error": str(exc)},
            ) from exc
        for stream_name, value in (("stdout", result.stdout), ("stderr", result.stderr)):
            for line in (value or "").splitlines():
                _emit(self.event_sink, "log", stream=stream_name, message=line)
        combined_output = "\n".join((result.stdout or "", result.stderr or "")).lower()
        allowed_nonzero = result.returncode != 0 and any(
            marker.lower() in combined_output for marker in allow_nonzero_with_markers
        )
        if result.returncode != 0 and not allowed_nonzero:
            raise DfuError(
                code,
                f"命令执行失败：{command[0]}",
                state=state,
                details={"returncode": result.returncode, "stderr": (result.stderr or "")[-2000:]},
            )
        if allowed_nonzero:
            _emit(
                self.event_sink,
                "log",
                level="warning",
                stream="app",
                message=(
                    f"{command[0]} 在下载完成后返回 {result.returncode}；"
                    "继续等待 UVC 恢复并以版本回读结果为准"
                ),
            )
        return result

    def _wait_for_dfu(self, port: str, timeout: float) -> DfuDevice:
        deadline = self.clock() + timeout
        while self.clock() < deadline:
            self.cancel_check()
            try:
                matches = [device for device in self.enumerate_dfu() if device.port == port]
            except OSError as exc:
                raise DfuError(
                    "dfu_enumeration_failed",
                    "读取 DFU USB 枚举状态失败",
                    state=DfuState.WAITING_FOR_DFU,
                    details={"error": str(exc)},
                ) from exc
            if matches:
                matching_alt = [device for device in matches if device.alt in (None, 0)]
                if matching_alt:
                    return matching_alt[0]
                raise DfuError(
                    "dfu_alt_zero_missing",
                    f"USB 端口 {port} 的 DFU 设备没有 alt=0",
                    state=DfuState.WAITING_FOR_DFU,
                    details={"devices": [item.to_dict() for item in matches]},
                )
            self.sleeper(self.poll_interval)
        raise DfuError(
            "dfu_enumeration_timeout",
            f"等待 USB 端口 {port} 枚举为 {DFU_DEVICE_ID} 超时",
            state=DfuState.WAITING_FOR_DFU,
            details={"port": port, "timeout": timeout},
        )

    def _wait_for_uvc(self, side: str, port: str, timeout: float) -> None:
        deadline = self.clock() + timeout
        while self.clock() < deadline:
            self.cancel_check()
            if self.camera_present(side):
                try:
                    if self.locate_uvc_port(side) == port:
                        return
                except DfuError:
                    pass
            self.sleeper(self.poll_interval)
        raise DfuError(
            "uvc_reenumeration_timeout",
            f"等待 {side} 主摄在 USB 端口 {port} 恢复为 UVC 超时",
            state=DfuState.WAITING_FOR_UVC,
            details={"side": side, "port": port, "timeout": timeout},
        )

    def upgrade(
        self,
        side: str,
        image_path: Path | str,
        *,
        expected_image: FirmwareImage | None = None,
        force: bool = False,
        dfu_timeout: float = 15.0,
        flash_timeout: float = 180.0,
        uvc_timeout: float = 30.0,
    ) -> DfuResult:
        try:
            if side not in CAMERA_DEVICE_BY_SIDE:
                raise DfuError(
                    "invalid_camera_side",
                    f"未知主摄侧：{side}",
                    state=DfuState.VALIDATING,
                    details={"allowed": sorted(CAMERA_DEVICE_BY_SIDE)},
                )
            self._transition(DfuState.VALIDATING, "校验主摄固件")
            image_path_resolved = validate_firmware_path(image_path, self.firmware_dir)
            self._transition(DfuState.PREREQUISITES, "检查主摄版本读取工具")
            reader = find_version_reader(self.version_reader)

            self._transition(DfuState.READING_VERSION, "读取镜像与当前设备版本")
            image = FirmwareImage(
                path=str(image_path_resolved),
                name=image_path_resolved.name,
                size=image_path_resolved.stat().st_size,
                sha256=sha256_file(image_path_resolved),
                version=read_image_version(
                    image_path_resolved,
                    version_reader=reader,
                    runner=self.runner,
                ),
            )
            _emit(self.event_sink, "firmware", firmware=image.to_dict())
            if expected_image is not None and image != expected_image:
                raise DfuError(
                    "firmware_changed_after_confirmation",
                    "主摄固件与操作员确认时的镜像不一致，拒绝继续",
                    state=DfuState.READING_VERSION,
                    details={
                        "confirmed": expected_image.to_dict(),
                        "current": image.to_dict(),
                    },
                )
            if self.read_device_version is None:
                raise PrerequisiteError(
                    "device_version_reader_missing",
                    "缺少可按 /dev/cam_left 或 /dev/cam_right 读取版本的接口",
                )
            previous_version = self.read_device_version(side)
            if versions_equal(image.version, previous_version) and not force:
                result = DfuResult(
                    side=side,
                    usb_port="",
                    image=image,
                    previous_version=previous_version,
                    installed_version=previous_version,
                    regression={"ok": True, "skipped": True},
                    command=(),
                    skipped=True,
                    skip_reason="version_match",
                )
                self._transition(
                    DfuState.COMPLETE,
                    f"当前版本已是 {previous_version}，跳过主摄 DFU 升级",
                )
                _emit(self.event_sink, "result", result=result.to_dict())
                return result

            self._transition(DfuState.PREREQUISITES, "检查主摄 DFU 写入工具")
            trigger = find_dfu_trigger(self.dfu_trigger)
            dfu_util = find_dfu_util(self.dfu_util)

            self._transition(DfuState.LOCATING_UVC, "锁定主摄 USB 物理端口")
            port = self.locate_uvc_port(side)
            _emit(self.event_sink, "device", side=side, usb_port=port, version=previous_version)

            assert_firmware_unchanged(
                image,
                image_path_resolved,
                code="firmware_changed_before_dfu",
                state=DfuState.LOCATING_UVC,
            )

            self._transition(DfuState.ENTERING_DFU, "触发主摄进入 DFU")
            self._run(
                [trigger, "--device", CAMERA_DEVICE_BY_SIDE[side]],
                timeout=15,
                code="dfu_trigger_failed",
                state=DfuState.ENTERING_DFU,
            )

            self._transition(DfuState.WAITING_FOR_DFU, "等待 DFU 枚举")
            self._wait_for_dfu(port, dfu_timeout)

            self._transition(DfuState.FLASHING, "写入主摄固件")
            assert_firmware_unchanged(image, image_path_resolved)
            command = build_dfu_command(dfu_util, port, image_path_resolved)
            flash_result = self._run(
                command,
                timeout=flash_timeout,
                code="dfu_flash_failed",
                state=DfuState.FLASHING,
                allow_nonzero_with_markers=("download done", "done!"),
            )
            flash_output = "\n".join((flash_result.stdout or "", flash_result.stderr or "")).lower()
            if "download done" not in flash_output and "done!" not in flash_output:
                raise DfuError(
                    "dfu_flash_completion_missing",
                    "dfu-util 返回成功但没有检测到写入完成标记",
                    state=DfuState.FLASHING,
                    details={"output": flash_output[-2000:]},
                )

            self._transition(DfuState.WAITING_FOR_UVC, "等待主摄恢复 UVC")
            self._wait_for_uvc(side, port, uvc_timeout)

            self._transition(DfuState.VERIFYING_VERSION, "核对升级后版本")
            installed_version = self.read_device_version(side)
            if not versions_equal(image.version, installed_version):
                raise DfuError(
                    "firmware_version_mismatch",
                    "主摄升级后版本与镜像版本不一致",
                    state=DfuState.VERIFYING_VERSION,
                    details={"expected": image.version, "actual": installed_version},
                )

            self._transition(DfuState.REGRESSION_CHECK, "执行主摄回归强检")
            if self.regression_check is None:
                raise PrerequisiteError(
                    "regression_check_missing",
                    "缺少主摄 SN、iSerial、MaxPacket 回归检查接口",
                )
            regression = dict(self.regression_check(side))
            if not regression.get("ok", False):
                raise DfuError(
                    "regression_check_failed",
                    "主摄升级后回归强检失败",
                    state=DfuState.REGRESSION_CHECK,
                    details=regression,
                )

            result = DfuResult(
                side=side,
                usb_port=port,
                image=image,
                previous_version=previous_version,
                installed_version=installed_version,
                regression=regression,
                command=tuple(command),
            )
            self._transition(DfuState.COMPLETE, "主摄固件升级完成")
            _emit(self.event_sink, "result", result=result.to_dict())
            return result
        except DfuError as exc:
            if exc.state is None and self.state is not None:
                exc.state = self.state.value
            self.state = DfuState.FAILED
            _state(self.event_sink, DfuState.FAILED, exc.message)
            _emit(self.event_sink, "error", **{k: v for k, v in exc.to_dict().items() if k != "type"})
            raise
        except Exception as exc:
            if exc.__class__.__name__ == "CancelledError":
                raise
            failed_at = self.state.value if self.state else None
            error = DfuError(
                "dfu_unexpected_error",
                f"主摄 DFU 流程异常：{exc}",
                state=failed_at,
                details={"class": exc.__class__.__name__},
            )
            self.state = DfuState.FAILED
            _state(self.event_sink, DfuState.FAILED, error.message)
            _emit(self.event_sink, "error", **{k: v for k, v in error.to_dict().items() if k != "type"})
            raise error from exc
