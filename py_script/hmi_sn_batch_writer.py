#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import shutil
import re
import sys
import time
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable
from xml.etree import ElementTree as ET

SCRIPT_DIR = Path(__file__).resolve().parent
TOOLKIT_DIRS = [
    SCRIPT_DIR,
    SCRIPT_DIR.parents[0] / "scripts" / "lib",
]
for toolkit_dir in TOOLKIT_DIRS:
    if str(toolkit_dir) not in sys.path:
        sys.path.insert(0, str(toolkit_dir))

from gripper_hmi_toolkit import ClientOptions, GripperHmiClient, detect_existing_ports


NS = {
    "a": "http://schemas.openxmlformats.org/spreadsheetml/2006/main",
    "r": "http://schemas.openxmlformats.org/officeDocument/2006/relationships",
}

ANSI_RESET = "\033[0m"
ANSI_RED = "\033[31m"
ANSI_GREEN = "\033[32m"
ANSI_YELLOW = "\033[33m"
ANSI_CYAN = "\033[36m"
HMI_PROBE_ROUNDS = 4
HMI_PROBE_ROUND_INTERVAL_S = 0.35

XLSX_SN_HEADER_KEYWORDS = ("sn", "sn码", "序列号", "serial")


def colorize(text: str, color: str) -> str:
    return f"{color}{text}{ANSI_RESET}"


def print_info(text: str) -> None:
    print(colorize(text, ANSI_CYAN))


def print_warn(text: str) -> None:
    print(colorize(text, ANSI_YELLOW))


def print_error(text: str) -> None:
    print(colorize(text, ANSI_RED))


def print_success(text: str) -> None:
    print(colorize(text, ANSI_GREEN))


def normalize_sn(value: str) -> str:
    return value.strip()


def fold_sn(value: str) -> str:
    return normalize_sn(value).upper()


def is_windows() -> bool:
    return os.name == "nt"


def iter_windows_existing_com_ports() -> list[str]:
    return sorted(detect_existing_ports(windows_wch_only=True), key=natural_port_sort_key)


def natural_port_sort_key(port: str) -> tuple[int, int, str]:
    text = port.upper()
    match = re.search(r"(\d+)$", text)
    number = int(match.group(1)) if match else 10**9
    if text.startswith("COM"):
        group = 0
    elif "TTYCH9344USB" in text:
        group = 1
    elif "TTYUSB" in text:
        group = 2
    elif "TTYACM" in text:
        group = 3
    else:
        group = 9
    return (group, number, text)


def guess_is_sn(value: str) -> bool:
    text = normalize_sn(value)
    return bool(re.fullmatch(r"[A-Za-z0-9]{8,32}", text))


def column_letters_to_index(ref: str) -> int:
    letters = "".join(ch for ch in ref if ch.isalpha()).upper()
    index = 0
    for ch in letters:
        index = index * 26 + (ord(ch) - ord("A") + 1)
    return max(index - 1, 0)


def iter_xlsx_rows(path: Path) -> list[tuple[str, list[str]]]:
    workbook_rows: list[tuple[str, list[str]]] = []
    with zipfile.ZipFile(path) as archive:
        shared_strings: list[str] = []
        if "xl/sharedStrings.xml" in archive.namelist():
            shared_root = ET.fromstring(archive.read("xl/sharedStrings.xml"))
            for item in shared_root.findall("a:si", NS):
                text = "".join(node.text or "" for node in item.iterfind(".//a:t", NS))
                shared_strings.append(text)

        workbook_root = ET.fromstring(archive.read("xl/workbook.xml"))
        rels_root = ET.fromstring(archive.read("xl/_rels/workbook.xml.rels"))
        rel_targets = {rel.attrib["Id"]: rel.attrib["Target"].lstrip("/") for rel in rels_root}

        sheets = workbook_root.find("a:sheets", NS)
        if sheets is None:
            return workbook_rows

        for sheet in sheets:
            sheet_name = sheet.attrib["name"]
            relation_id = sheet.attrib["{http://schemas.openxmlformats.org/officeDocument/2006/relationships}id"]
            target = rel_targets[relation_id]
            sheet_root = ET.fromstring(archive.read(target))

            for row in sheet_root.findall(".//a:sheetData/a:row", NS):
                values_by_index: dict[int, str] = {}
                max_index = -1
                for cell in row.findall("a:c", NS):
                    ref = cell.attrib.get("r", "")
                    cell_type = cell.attrib.get("t", "")
                    cell_value = ""

                    inline_string = cell.find("a:is", NS)
                    value_node = cell.find("a:v", NS)
                    if inline_string is not None:
                        cell_value = "".join(node.text or "" for node in inline_string.iterfind(".//a:t", NS))
                    elif value_node is not None and value_node.text is not None:
                        raw_value = value_node.text
                        if cell_type == "s":
                            try:
                                cell_value = shared_strings[int(raw_value)]
                            except (ValueError, IndexError):
                                cell_value = raw_value
                        else:
                            cell_value = raw_value

                    index = column_letters_to_index(ref)
                    values_by_index[index] = cell_value.strip()
                    max_index = max(max_index, index)

                if max_index < 0:
                    continue

                ordered = [values_by_index.get(index, "").strip() for index in range(max_index + 1)]
                workbook_rows.append((sheet_name, ordered))
    return workbook_rows


@dataclass(frozen=True)
class SnRecord:
    sn: str
    source_file: Path
    sheet_name: str
    row_index: int


def load_sn_records(xlsx_paths: Iterable[Path]) -> list[SnRecord]:
    records: list[SnRecord] = []
    seen: set[str] = set()
    for xlsx_path in xlsx_paths:
        row_index_per_sheet: dict[str, int] = {}
        header_map_per_sheet: dict[str, dict[int, str]] = {}
        for sheet_name, row in iter_xlsx_rows(xlsx_path):
            row_index_per_sheet[sheet_name] = row_index_per_sheet.get(sheet_name, 0) + 1
            sheet_row_index = row_index_per_sheet[sheet_name]
            normalized = [cell.strip() for cell in row]

            if sheet_name not in header_map_per_sheet:
                header_map_per_sheet[sheet_name] = {}

            header_map = header_map_per_sheet[sheet_name]
            if any(cell for cell in normalized):
                for index, cell in enumerate(normalized):
                    lowered = cell.lower().replace(" ", "")
                    if any(keyword in lowered for keyword in XLSX_SN_HEADER_KEYWORDS):
                        header_map[index] = cell

            candidate_indices = list(header_map.keys())
            if not candidate_indices:
                continue

            for index in candidate_indices:
                if index >= len(normalized):
                    continue
                candidate = normalize_sn(normalized[index])
                if not guess_is_sn(candidate):
                    continue
                candidate_folded = fold_sn(candidate)
                if candidate_folded in seen:
                    continue
                records.append(
                    SnRecord(
                        sn=candidate,
                        source_file=xlsx_path,
                        sheet_name=sheet_name,
                        row_index=sheet_row_index,
                    )
                )
                seen.add(candidate_folded)
    return sorted(records, key=lambda item: item.sn)


def find_matches(records: list[SnRecord], fragment: str) -> list[SnRecord]:
    key = fold_sn(fragment)
    return [record for record in records if key in fold_sn(record.sn)]


def locate_xlsx_files(paths: list[str], search_root: Path) -> list[Path]:
    if paths:
        resolved = [Path(path).expanduser().resolve() for path in paths]
        return [path for path in resolved if path.is_file()]

    candidates = sorted(search_root.glob("*.xlsx"))
    if candidates:
        return candidates

    cwd_candidates = sorted(Path.cwd().glob("*.xlsx"))
    return [path.resolve() for path in cwd_candidates]


def helper_read_sn(port: str) -> str:
    with GripperHmiClient(ClientOptions(port=port)) as client:
        raw_sn = client.read_serial_number()
        sn = normalize_sn(raw_sn)
    if not guess_is_sn(sn):
        raise RuntimeError(
            f"驱动返回了非法 SN: raw={raw_sn!r} normalized={sn!r} length={len(sn)}"
        )
    return sn


def helper_write_sn(port: str, sn: str) -> None:
    with GripperHmiClient(ClientOptions(port=port)) as client:
        ack_summary = client.write_serial_number(sn)
    print_info(f"SN 写入 ACK: {ack_summary.to_text()}")


def overwrite_sn_with_verify(port: str, target_sn: str, max_attempts: int = 4) -> str:
    last_error = "unknown"
    for attempt in range(1, max_attempts + 1):
        try:
            helper_write_sn(port, target_sn)
            time.sleep(0.18)
            verify_sn = helper_read_sn(port)
            if fold_sn(verify_sn) != fold_sn(target_sn):
                raise RuntimeError(f"SN 回读不一致，期望 {target_sn}，实际 {verify_sn}")
            return verify_sn
        except Exception as exc:
            last_error = str(exc)
            time.sleep(0.25)
    raise RuntimeError(f"SN 覆盖写入失败，重试 {max_attempts} 次后仍未成功: {last_error}")


@dataclass
class ProbeResult:
    port: str
    serial_number: str
    sn_initialized: bool


def probe_sn_state(port: str) -> ProbeResult:
    with GripperHmiClient(ClientOptions(port=port)) as client:
        raw_sn = client.read_serial_number()
    sn = normalize_sn(raw_sn)
    if guess_is_sn(sn):
        return ProbeResult(port=port, serial_number=sn, sn_initialized=True)
    if sn == "":
        return ProbeResult(port=port, serial_number="", sn_initialized=False)
    raise RuntimeError(
        f"驱动返回了非法 SN: raw={raw_sn!r} normalized={sn!r} length={len(sn)}"
    )


def iter_candidate_ports() -> list[str]:
    if is_windows():
        return iter_windows_existing_com_ports()

    candidates: list[str] = []
    for pattern in ("/dev/ttyCH9344USB*", "/dev/ttyUSB*", "/dev/ttyACM*"):
        for path in sorted(Path("/").glob(pattern.lstrip("/"))):
            resolved = str(path.resolve()) if path.exists() else str(path)
            if resolved not in candidates:
                candidates.append(resolved)
    return sorted(set(candidates), key=natural_port_sort_key)


def probe_hmi_port(baudrate: int) -> ProbeResult:
    del baudrate
    last_errors: list[str] = []
    ports = iter_candidate_ports()
    if not ports:
        if is_windows():
            raise RuntimeError("未扫描到可用 HMI 端口。Windows 下仅扫描 WCH USB-SERIAL Ch 对应的 COM 口。")
        raise RuntimeError("未扫描到可用 HMI 端口。")
    # 两个平台保持一致的探测节奏；只在端口枚举来源上区分平台。
    for round_index in range(HMI_PROBE_ROUNDS):
        for port in ports:
            try:
                probe = probe_sn_state(port)
                return probe
            except Exception as exc:
                last_errors.append(f"round={round_index + 1} {port}: {exc}")
                continue
        time.sleep(HMI_PROBE_ROUND_INTERVAL_S)
    detail = "\n".join(last_errors[-12:])
    raise RuntimeError(f"未扫描到可用 HMI 端口。\n{detail}")


def ensure_sn_unique(matches: list[SnRecord], fragment: str) -> SnRecord | None:
    if not matches:
        print_warn(f"片段 [{fragment}] 没有匹配到任何 SN。")
        return None
    if len(matches) > 1:
        print_warn(f"片段 [{fragment}] 匹配到 {len(matches)} 个 SN，请继续缩小范围：")
        for match in matches[:12]:
            print(f"  - {match.sn}  ({match.source_file.name}/{match.sheet_name}#{match.row_index})")
        if len(matches) > 12:
            print(f"  ... 其余 {len(matches) - 12} 个未展示")
        return None
    return matches[0]


def resolve_target_record(
    records: list[SnRecord],
    fragment: str,
    forced_sn: str,
) -> tuple[str, SnRecord | None]:
    matches = find_matches(records, fragment)
    if matches:
        target = ensure_sn_unique(matches, fragment)
        if target is None:
            return "", None
        return target.sn, target

    if forced_sn:
        normalized_fragment = fold_sn(fragment)
        if normalized_fragment and normalized_fragment not in fold_sn(forced_sn):
            print_warn(
                f"片段 [{fragment}] 未命中表格，也不匹配特殊指定 SN [{forced_sn}]。"
            )
            return "", None
        print_warn(
            f"片段 [{fragment}] 未在表格中找到匹配项，改为使用特殊指定 SN: {forced_sn}"
        )
        return forced_sn, None

    print_warn(f"片段 [{fragment}] 没有匹配到任何 SN。")
    return "", None


def validate_sn_fragment(fragment: str) -> bool:
    normalized = fold_sn(fragment)
    if len(normalized) < 4:
        print_warn(f"输入片段 [{fragment}] 长度不足 4 位，请输入至少 4 位连续片段。")
        return False
    return True


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="连续向夹爪 HMI 写入并校验 SN；力传感器标定由 HMI 监视/标定工具单独完成。"
    )
    parser.add_argument("--xlsx", nargs="*", default=[], help="一个或多个 xlsx 文件路径")
    parser.add_argument(
        "--search-root",
        default=None,
        help="默认从脚本所在目录搜索 xlsx；通常不需要手动指定",
    )
    parser.add_argument(
        "--calib-root",
        default=None,
        help="已弃用；相机/IMU 标定不再写入 HMI payload",
    )
    parser.add_argument("--baudrate", type=int, default=115200, help="HMI 串口波特率")
    parser.add_argument(
        "--skip-calibration",
        action="store_true",
        help="兼容旧命令；当前始终只写 SN",
    )
    parser.add_argument(
        "--force-sn",
        default="",
        help="强制写入一个表格中不存在的完整目标 SN；若输入片段能命中该 SN，则可直接写入，且无标定文件时也允许只写 SN",
    )
    return parser.parse_args()


def build_delivery_readme(delivery_dir: Path) -> str:
    script_name = "hmi_sn_batch_writer.py"
    port_scan_text = (
        "- Linux: 遍历 `/dev/ttyCH9344USB*`、`/dev/ttyUSB*`、`/dev/ttyACM*`\n"
        "- Windows: 仅遍历当前系统中识别为 `WCH USB-SERIAL Ch... (COMx)` 的端口"
    )
    return f"""# HMI SN Batch Writer Delivery

## 目录约定
- 将表格 `.xlsx` 与 `{script_name}` 放在同一目录
- HMI Python 驱动固定为同目录下的 `gripper_hmi_toolkit.py`
- 本工具只写入并校验 SN，不读取或改写 HMI 力标定 payload

## 典型目录
```text
{delivery_dir.name}/
  {script_name}
  gripper_hmi_toolkit.py
  *.xlsx
```

## 用法
```bash
python3 {script_name}
```

## 交互规则
- 每次只插入一个夹爪
- 串口扫描规则：
{port_scan_text}
- 输入任意连续片段，但长度必须 >= 4
- 若匹配到多个 SN，会报错并要求重新输入
- 若匹配到唯一 SN，会直接开始：
  1. 覆盖 SN
  2. 回读确认 SN
- 已有 HMI 力标定 payload 保持不变

## 特殊指定 SN
- 若要写入一个表格中不存在的完整 SN，使用：
```bash
python3 {script_name} --force-sn <完整SN>
```
- 输入片段需要能命中该完整 SN，脚本会直接使用这个特殊指定 SN
"""


def create_delivery_bundle(project_root: Path) -> Path:
    delivery_dir = project_root / "tmp" / "hmi_sn_writer_delivery"
    if delivery_dir.exists():
        shutil.rmtree(delivery_dir)
    delivery_dir.mkdir(parents=True, exist_ok=True)

    copies = [
        (project_root / "py_script" / "hmi_sn_batch_writer.py", delivery_dir / "hmi_sn_batch_writer.py"),
        (project_root / "scripts" / "lib" / "gripper_hmi_toolkit.py", delivery_dir / "gripper_hmi_toolkit.py"),
        (project_root / "内部v2爪2026-03-10_19-14_G9_SN码.xlsx", delivery_dir / "内部v2爪2026-03-10_19-14_G9_SN码.xlsx"),
        (project_root / "联想v2爪2026-03-11_13-45_G9_SN码.xlsx", delivery_dir / "联想v2爪2026-03-11_13-45_G9_SN码.xlsx"),
    ]
    for source, target in copies:
        if source.is_file():
            shutil.copy2(source, target)

    readme_path = delivery_dir / "README.md"
    readme_path.write_text(build_delivery_readme(delivery_dir), encoding="utf-8")
    return delivery_dir


def main() -> int:
    args = parse_args()
    script_path = Path(__file__).resolve()
    bundle_root = script_path.parent
    search_root = Path(args.search_root).expanduser().resolve() if args.search_root else bundle_root
    forced_sn = normalize_sn(args.force_sn) if args.force_sn else ""

    xlsx_paths = locate_xlsx_files(args.xlsx, search_root)
    if not xlsx_paths and not forced_sn:
        print_error("未找到任何 xlsx 文件。请把表格和脚本放在同一目录，或通过 --xlsx 指定。")
        return 2

    records = load_sn_records(xlsx_paths) if xlsx_paths else []
    if xlsx_paths and not records and not forced_sn:
        print_error("已找到 xlsx，但未识别到任何 SN 记录。请确认表头里存在 `SN码` / `SN` 列。")
        return 2

    if xlsx_paths:
        print_info(f"已加载 {len(xlsx_paths)} 个表格，共识别 {len(records)} 条唯一 SN。")
        for xlsx_path in xlsx_paths:
            print(f"  - {xlsx_path}")
    else:
        print_warn("未加载任何表格，将仅允许通过 --force-sn 指定完整 SN 写入。")
    print_info("HMI payload 仅保留 SN 绑定的力传感器标定，本工具不会改写该 payload。")

    while True:
        first_input = input(
            "\n插入一个夹爪后按回车开始扫描，输入 SN 任意连续片段(至少4位)可直接匹配，输入 q 退出: "
        ).strip()
        if first_input.lower() in {"q", "quit", "exit"}:
            print_info("已退出。")
            return 0

        try:
            probe = probe_hmi_port(args.baudrate)
            if probe.sn_initialized:
                print_info(f"检测到 HMI 端口: {probe.port}，当前读到 SN: {probe.serial_number}")
            else:
                print_info(f"检测到 HMI 端口: {probe.port}，当前 SN: <未初始化>")
        except Exception as exc:
            print_error(str(exc))
            continue

        fragment = first_input or input("请输入目标 SN 任意连续片段(至少4位): ").strip()
        if fragment.lower() in {"q", "quit", "exit"}:
            print_info("已退出。")
            return 0
        if not fragment:
            print_warn("输入为空，本轮跳过。")
            continue
        if not validate_sn_fragment(fragment):
            continue

        target_sn, target = resolve_target_record(records, fragment, forced_sn)
        if not target_sn:
            continue

        print_info(f"唯一匹配到目标 SN: {target_sn}")
        if target is not None:
            print(f"  来源: {target.source_file.name}/{target.sheet_name}#{target.row_index}")
        else:
            print(f"  来源: 特殊指定 SN (--force-sn)")
        print_info("相机/IMU 标定保存在相机模组；本工具不会改写 HMI 力标定 payload。")

        try:
            verify_sn = overwrite_sn_with_verify(probe.port, target_sn)
            action_text = f"写入成功 SN={target_sn} SN=已覆盖并校验 HMI力标定=未改写"
            print_success(action_text)
            print_success(f"回读 SN: {verify_sn}")
        except Exception as exc:
            print_error(f"写入失败 SN={target_sn}，原因: {exc}")
            continue


if __name__ == "__main__":
    raise SystemExit(main())
