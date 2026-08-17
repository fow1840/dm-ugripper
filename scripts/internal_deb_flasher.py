#!/usr/bin/env python3
from __future__ import annotations

import bz2
import gzip
import io
import lzma
import os
import re
import shlex
import sys
import tarfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, Tuple

try:
    import paramiko
    import zstandard
except ImportError:
    print("缺少依赖，请执行: py -m pip install paramiko zstandard")
    raise SystemExit(2)


TARGET_HOST = "192.168.2.240"
TARGET_USER = "ubuntu"
TARGET_PASSWORD = "ubuntu"

PACKAGE_ORDER = (
    "ugripper",
    "das-usb-updater",
    "bluetooth-gatt-server",
    "databot-device-joint",
    "device-ota-mender",
)
PACKAGE_SET = frozenset(PACKAGE_ORDER)
SAFE_DEB_NAME = re.compile(r"^[A-Za-z0-9._+~-]+\.deb$")
VERSION_MARKER = "__UGRIPPER_PKG__\t"


@dataclass(frozen=True)
class PackageInfo:
    path: Path
    name: str
    version: str
    architecture: str


def sanitize(value: str) -> str:
    for secret, replacement in (
        (TARGET_HOST, "[target]"),
        (TARGET_USER, "[user]"),
        (TARGET_PASSWORD, "[secret]"),
    ):
        value = value.replace(secret, replacement)
    return value


def ar_members(path: Path) -> Iterable[Tuple[str, bytes]]:
    with path.open("rb") as source:
        if source.read(8) != b"!<arch>\n":
            raise ValueError("不是有效的 Debian ar 文件")
        while True:
            header = source.read(60)
            if not header:
                return
            if len(header) != 60 or header[58:60] != b"`\n":
                raise ValueError("deb ar 成员头损坏")
            try:
                size = int(header[48:58].decode("ascii").strip())
                name = header[:16].decode("utf-8").strip()
            except (UnicodeDecodeError, ValueError) as error:
                raise ValueError("deb ar 成员头非法") from error
            data = source.read(size)
            if len(data) != size:
                raise ValueError("deb ar 成员内容不完整")
            if size % 2:
                source.read(1)
            if name.startswith("#1/"):
                name_size = int(name[3:])
                name = data[:name_size].decode("utf-8").rstrip("\0")
                data = data[name_size:]
            else:
                name = name.rstrip("/")
            yield name, data


def decompress_control(name: str, data: bytes) -> bytes:
    if name.endswith(".gz"):
        return gzip.decompress(data)
    if name.endswith(".xz"):
        return lzma.decompress(data)
    if name.endswith(".bz2"):
        return bz2.decompress(data)
    if name.endswith((".zst", ".zstd")):
        with zstandard.ZstdDecompressor().stream_reader(io.BytesIO(data)) as reader:
            return reader.read()
    if name == "control.tar":
        return data
    raise ValueError(f"不支持的 control 压缩格式: {name}")


def parse_control_fields(raw: str) -> Dict[str, str]:
    fields: Dict[str, str] = {}
    current = ""
    for line in raw.splitlines():
        if line.startswith((" ", "\t")) and current:
            fields[current] += "\n" + line[1:]
            continue
        if ":" not in line:
            current = ""
            continue
        current, value = line.split(":", 1)
        fields[current] = value.strip()
    return fields


def read_deb_metadata(path: Path) -> PackageInfo:
    control_member = next(
        ((name, data) for name, data in ar_members(path) if name.startswith("control.tar")),
        None,
    )
    if control_member is None:
        raise ValueError("deb 缺少 control.tar")
    control_name, control_data = control_member
    tar_data = decompress_control(control_name, control_data)
    with tarfile.open(fileobj=io.BytesIO(tar_data), mode="r:") as archive:
        member = next(
            (item for item in archive.getmembers() if item.name.lstrip("./") == "control"),
            None,
        )
        if member is None:
            raise ValueError("deb control.tar 缺少 control 文件")
        extracted = archive.extractfile(member)
        if extracted is None:
            raise ValueError("无法读取 deb control 文件")
        fields = parse_control_fields(extracted.read().decode("utf-8"))

    required = ("Package", "Version", "Architecture")
    if any(not fields.get(field) for field in required):
        raise ValueError("deb control 缺少 Package/Version/Architecture")
    if any("\t" in fields[field] or "\n" in fields[field] for field in required):
        raise ValueError("deb control 元数据非法")
    return PackageInfo(path.resolve(), fields["Package"], fields["Version"], fields["Architecture"])


def discover_packages(directory: Path) -> Tuple[PackageInfo, ...]:
    selected: Dict[str, PackageInfo] = {}
    for path in directory.iterdir():
        if not path.is_file() or path.suffix.lower() != ".deb":
            continue
        if not SAFE_DEB_NAME.fullmatch(path.name):
            raise ValueError(f"deb 文件名包含不支持的字符: {path.name}")
        try:
            package = read_deb_metadata(path)
        except Exception as error:
            raise ValueError(f"无法读取 deb 元数据: {path.name}: {error}") from error
        if package.name not in PACKAGE_SET:
            print(f"[SKIP] 非白名单 deb: {path.name} ({package.name})")
            continue
        if package.architecture not in ("arm64", "all"):
            raise ValueError(
                f"白名单包架构不是 arm64/all: {path.name} ({package.architecture})"
            )
        if package.name in selected:
            raise ValueError(f"同一软件包存在多个 deb，请只保留一个: {package.name}")
        selected[package.name] = package
    return tuple(selected[name] for name in PACKAGE_ORDER if name in selected)


def run_remote(
    client: paramiko.SSHClient, command: str, standard_input: str = ""
) -> Tuple[int, str]:
    transport = client.get_transport()
    if transport is None or not transport.is_active():
        raise RuntimeError("目标机 SSH 连接不可用")
    channel = transport.open_session(timeout=20)
    channel.exec_command(command)
    if standard_input:
        channel.sendall(standard_input.encode("utf-8"))
    channel.shutdown_write()

    stdout = bytearray()
    stderr = bytearray()
    while True:
        while channel.recv_ready():
            stdout.extend(channel.recv(65536))
        while channel.recv_stderr_ready():
            stderr.extend(channel.recv_stderr(65536))
        if channel.exit_status_ready() and not channel.recv_ready() and not channel.recv_stderr_ready():
            break
        time.sleep(0.02)
    status = channel.recv_exit_status()
    output = stdout.decode("utf-8", errors="replace") + stderr.decode(
        "utf-8", errors="replace"
    )
    return status, output


def require_remote_success(result: Tuple[int, str], context: str) -> str:
    status, output = result
    if status != 0:
        detail = sanitize(output.strip())
        raise RuntimeError(context + "失败" + (f":\n{detail}" if detail else ""))
    return output


def connect_target() -> paramiko.SSHClient:
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(
        hostname=TARGET_HOST,
        username=TARGET_USER,
        password=TARGET_PASSWORD,
        timeout=15,
        banner_timeout=15,
        auth_timeout=15,
        look_for_keys=False,
        allow_agent=False,
    )
    return client


def query_installed_versions(client: paramiko.SSHClient) -> Dict[str, str]:
    packages = " ".join(shlex.quote(name) for name in PACKAGE_ORDER)
    command = f"""for pkg in {packages}; do
  status="$(dpkg-query -W -f='${{Status}}' -- "$pkg" 2>/dev/null || true)"
  version=""
  if [ "$status" = "install ok installed" ]; then
    version="$(dpkg-query -W -f='${{Version}}' -- "$pkg")"
  fi
  printf '{VERSION_MARKER}%s\\t%s\\n' "$pkg" "$version"
done
"""
    output = require_remote_success(run_remote(client, command), "读取目标机软件版本")
    versions: Dict[str, str] = {}
    for line in output.splitlines():
        if not line.startswith(VERSION_MARKER):
            continue
        fields = line[len(VERSION_MARKER) :].split("\t")
        if len(fields) != 2 or fields[0] not in PACKAGE_SET or fields[0] in versions:
            raise RuntimeError("目标机软件版本返回内容非法")
        versions[fields[0]] = fields[1]
    if set(versions) != PACKAGE_SET:
        raise RuntimeError("目标机未返回完整的白名单软件版本")
    return versions


def print_package_list(
    packages: Tuple[PackageInfo, ...], installed_versions: Dict[str, str]
) -> None:
    local_packages = {package.name: package for package in packages}
    print("\n目标机白名单软件和本地安装包:")
    print("序号  软件包                         目标机版本               本地版本(架构)")
    for index, name in enumerate(PACKAGE_ORDER, 1):
        local = local_packages.get(name)
        installed = installed_versions[name]
        target_display = installed or "未安装"
        local_display = "-" if local is None else f"{local.version} ({local.architecture})"
        print(f"{index:<6}{name:<31}{target_display:<25}{local_display}")


def build_install_script(packages: Tuple[PackageInfo, ...], remote_directory: str) -> str:
    install_args = "\n".join(
        f"  {shlex.quote(remote_directory + '/' + package.path.name)}" for package in packages
    )
    expected_names = "\n".join(f"  {shlex.quote(package.name)}" for package in packages)
    expected_versions = "\n".join(
        f"  {shlex.quote(package.version)}" for package in packages
    )
    expected_arches = "\n".join(
        f"  {shlex.quote(package.architecture)}" for package in packages
    )
    return f"""set -euo pipefail
REMOTE_DIR={shlex.quote(remote_directory)}
cleanup() {{ rm -rf -- "$REMOTE_DIR"; }}
trap cleanup EXIT
target_arch="$(dpkg --print-architecture)"
target_machine="$(uname -m)"
[ "$target_arch" = arm64 ] || {{ echo '[INSTALL] fail: target is not arm64'; exit 30; }}
case "$target_machine" in aarch64|arm64) ;; *) echo '[INSTALL] fail: target machine is not ARM64'; exit 31 ;; esac
APT_DPKG_LOCK_TIMEOUT_SECONDS=300
wait_for_apt_dpkg_locks() {{
  local reason="${{1:-apt/dpkg}}" start now elapsed holders lock pids
  local locks=(/var/lib/dpkg/lock-frontend /var/lib/dpkg/lock /var/cache/apt/archives/lock /var/lib/apt/lists/lock)
  if ! command -v fuser >/dev/null 2>&1; then
    echo "[INSTALL] WARN fuser not found; skip apt/dpkg lock pre-wait for $reason"
    return 0
  fi
  start="$(date +%s)"
  while true; do
    holders=""
    for lock in "${{locks[@]}}"; do
      if [ -e "$lock" ] && fuser -s "$lock" 2>/dev/null; then
        pids="$(fuser "$lock" 2>/dev/null | tr -s ' ' ',' | sed 's/^,//;s/,$//')"
        holders="${{holders}} $lock:${{pids:-unknown}}"
      fi
    done
    [ -z "$holders" ] && return 0
    now="$(date +%s)"
    elapsed=$((now - start))
    if [ "$elapsed" -ge "$APT_DPKG_LOCK_TIMEOUT_SECONDS" ]; then
      echo "[INSTALL] fail: apt/dpkg lock still held after ${{APT_DPKG_LOCK_TIMEOUT_SECONDS}}s for $reason:$holders"
      return 32
    fi
    echo "[INSTALL] WAIT apt/dpkg lock for $reason (${{elapsed}}s):$holders"
    sleep 2
  done
}}
installed_package_version() {{
  local pkg="$1" status version
  IFS="$(printf '\\t')" read -r status version < <(dpkg-query -W -f='${{Status}}\\t${{Version}}' "$pkg" 2>/dev/null || true)
  if [ "$status" = "install ok installed" ] && [ -n "$version" ]; then printf '%s' "$version"; fi
}}
install_args=(
{install_args}
)
expected_names=(
{expected_names}
)
expected_versions=(
{expected_versions}
)
expected_arches=(
{expected_arches}
)
for i in "${{!install_args[@]}}"; do
  deb="${{install_args[$i]}}"
  actual_name="$(dpkg-deb -f "$deb" Package)"
  actual_version="$(dpkg-deb -f "$deb" Version)"
  actual_arch="$(dpkg-deb -f "$deb" Architecture)"
  if [ "$actual_name" != "${{expected_names[$i]}}" ] || [ "$actual_version" != "${{expected_versions[$i]}}" ]; then
    echo "[INSTALL] fail: uploaded deb metadata mismatch: $(basename "$deb")"; exit 33
  fi
  [ "$actual_arch" = "${{expected_arches[$i]}}" ] || {{ echo "[INSTALL] fail: uploaded deb architecture changed: $(basename "$deb")"; exit 34; }}
  case "$actual_arch" in arm64|all) ;; *) echo "[INSTALL] fail: unsupported deb architecture: $actual_arch"; exit 35 ;; esac
done
has_das=0
for pkg in "${{expected_names[@]}}"; do [ "$pkg" = das-usb-updater ] && has_das=1; done
if [ "$has_das" -eq 1 ]; then
  legacy_status="$(dpkg-query -W -f='${{Status}}' ugripper-usb-updater 2>/dev/null || true)"
  if [ "$legacy_status" = "install ok installed" ]; then
    echo '[INSTALL] remove legacy updater before das-usb-updater'
    wait_for_apt_dpkg_locks 'remove legacy updater'
    dpkg -r ugripper-usb-updater
  fi
fi
echo "[INSTALL] installing ${{#install_args[@]}} package(s)"
wait_for_apt_dpkg_locks 'dpkg install application packages'
dpkg --auto-deconfigure -i --force-overwrite "${{install_args[@]}}"
wait_for_apt_dpkg_locks 'apt-get fix dependencies'
apt-get -o DPkg::Lock::Timeout=300 -f install --no-download -y
for i in "${{!install_args[@]}}"; do
  pkg="${{expected_names[$i]}}"
  expected="${{expected_versions[$i]}}"
  installed="$(installed_package_version "$pkg")"
  if [ "$installed" != "$expected" ]; then
    echo "[INSTALL] fail: $pkg expected=$expected installed=${{installed:-not-installed}}"; exit 21
  fi
  echo "[INSTALL] verified $pkg=$installed"
  if [ "$pkg" = device-ota-mender ]; then
    echo '[MENDER] running mender-offline-init --force-install-module...'
    if command -v mender-offline-init >/dev/null 2>&1; then
      echo '[MENDER] wait 2s before mender-offline-init --force-install-module...'
      sleep 2
      wait_for_apt_dpkg_locks 'mender-offline-init'
      set +e
      mender-offline-init --force-install-module 2>&1 | sed 's/^/[MENDER] /'
      mender_rc="${{PIPESTATUS[0]}}"
      set -e
      [ "$mender_rc" -eq 0 ] || {{ echo "[MENDER] fail: mender-offline-init --force-install-module exit=$mender_rc"; exit "$mender_rc"; }}
      echo '[MENDER] mender-offline-init --force-install-module completed'
    else
      echo '[MENDER] fail: mender-offline-init not found'; exit 22
    fi
  fi
done
echo '[RESULT] installation completed'
for pkg in "${{expected_names[@]}}"; do echo "[RESULT] $pkg=$(installed_package_version "$pkg")"; done
"""


def upload_packages(
    client: paramiko.SSHClient,
    packages: Tuple[PackageInfo, ...],
    remote_directory: str,
) -> None:
    require_remote_success(
        run_remote(
            client,
            f"mkdir -p -- {shlex.quote(remote_directory)} && chmod 700 -- {shlex.quote(remote_directory)}",
        ),
        "创建远端临时目录",
    )
    try:
        with client.open_sftp() as sftp:
            for package in packages:
                remote_path = remote_directory + "/" + package.path.name
                sftp.put(str(package.path), remote_path)
                sftp.chmod(remote_path, 0o600)
    except Exception:
        run_remote(client, f"rm -rf -- {shlex.quote(remote_directory)}")
        raise


def main() -> int:
    scan_directory = Path(__file__).resolve().parent
    packages = discover_packages(scan_directory)
    if not packages:
        raise RuntimeError("脚本同级目录没有可安装的白名单 deb")

    print("[1/4] 正在检查目标设备架构...")
    with connect_target() as client:
        architecture_command = (
            "a=$(dpkg --print-architecture 2>/dev/null || true); "
            "m=$(uname -m 2>/dev/null || true); "
            "if [ \"$a\" = arm64 ] && { [ \"$m\" = aarch64 ] || [ \"$m\" = arm64 ]; }; "
            "then printf 'ARM64_OK\\n'; else printf 'ARM64_FAIL arch=%s machine=%s\\n' \"$a\" \"$m\"; exit 40; fi"
        )
        architecture = require_remote_success(
            run_remote(client, architecture_command), "目标设备架构检查"
        )
        if "ARM64_OK" not in architecture:
            raise RuntimeError("目标设备未返回 ARM64 确认")

        print("[1/4] 正在读取目标机白名单软件版本...")
        installed_versions = query_installed_versions(client)
        print_package_list(packages, installed_versions)
        confirmation = input("\n输入 INSTALL 确认升级/安装，其他输入取消: ")
        if confirmation != "INSTALL":
            print("已取消，未上传或安装软件包。")
            return 0

        remote_directory = f"/tmp/ugripper-internal-install-{os.urandom(8).hex()}"
        print(f"[2/4] 正在上传 {len(packages)} 个软件包...")
        upload_packages(client, packages, remote_directory)
        print("[3/4] 正在安装并校验软件包...")
        install_script = build_install_script(packages, remote_directory)
        result = run_remote(
            client,
            "sudo -S -k -p '' bash -s",
            TARGET_PASSWORD + "\n" + install_script,
        )
        print(sanitize(result[1]), end="" if result[1].endswith("\n") else "\n")
        require_remote_success(result, "远端安装")
        print("[4/4] 烧录完成。")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\n已取消。")
        raise SystemExit(130)
    except Exception as error:
        print(f"[ERROR] {sanitize(str(error))}", file=sys.stderr)
        raise SystemExit(1)
