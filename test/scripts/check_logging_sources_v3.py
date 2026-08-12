#!/usr/bin/env python3
"""Static guard for V3 error APIs and classified main-log sources."""

from __future__ import annotations

import csv
import ast
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PRODUCTION_DIRS = (ROOT / "standalone", ROOT / "src")
LEGACY_ERROR_RE = re.compile(r"\bDM_LOG_(?:ERROR|CRITICAL)\s*\(")


def main() -> int:
    failures: list[str] = []
    for directory in PRODUCTION_DIRS:
        for path in directory.rglob("*"):
            if path.suffix not in (".cc", ".cpp", ".h", ".hpp") or "third_party" in path.parts:
                continue
            if path == ROOT / "src/utils/include/utils/logger.h":
                continue
            for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
                if LEGACY_ERROR_RE.search(line):
                    failures.append(f"{path.relative_to(ROOT)}:{number}: use explicit *_CODE API")

    python_roots = (ROOT / "audio", ROOT / "standalone/UgripperRuntime/audio",
                    ROOT / "standalone/UgripperRuntime/ego", ROOT / "scripts/restore_usb")
    for directory in python_roots:
        for path in directory.glob("*.py"):
            tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
            for node in ast.walk(tree):
                if not isinstance(node, ast.Call) or not node.args:
                    continue
                name = node.func.id if isinstance(node.func, ast.Name) else ""
                if name not in ("audio_log", "ugripper_log"):
                    continue
                level = node.args[0].value if isinstance(node.args[0], ast.Constant) else None
                if level not in ("ERROR", "FATAL", "CRITICAL"):
                    continue
                has_keyword = any(keyword.arg == "error_code" for keyword in node.keywords)
                has_audio_positional = name == "audio_log" and len(node.args) >= 3
                if not has_keyword and not has_audio_positional:
                    failures.append(
                        f"{path.relative_to(ROOT)}:{node.lineno}: Python E/F requires explicit error_code"
                    )

    shell_paths = (
        ROOT / "run_record.sh",
        ROOT / "auto_calibration/run_calibration.sh",
        ROOT / "auto_calibration/import_camera_calibration.sh",
        ROOT / "time_sync/safe_ntp_sync.sh",
    )
    for path in shell_paths:
        logical_lines: list[tuple[int, str]] = []
        pending = ""
        start = 0
        for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if not pending:
                start = number
            pending += raw.rstrip("\\") + " "
            if raw.endswith("\\"):
                continue
            logical_lines.append((start, pending))
            pending = ""
        for number, line in logical_lines:
            has_code_argument = "UGR-" in line or "$error_code" in line or '"$code"' in line
            if "ugripper_log" in line and '"ERROR"' in line and not has_code_argument:
                failures.append(
                    f"{path.relative_to(ROOT)}:{number}: Shell E/F requires explicit error code"
                )

    ledger = ROOT / "test/data/logging/ugripper_logging_input_ledger_v3.tsv"
    with ledger.open(encoding="utf-8", newline="") as file:
        rows = list(csv.DictReader(file, delimiter="\t"))
    for row in rows:
        if row["phase0_status"] != "classified":
            failures.append(f"ledger source {row['source_id']} is not classified")
        if row["main_log_scope"].startswith("main_log") and not row["target_route"]:
            failures.append(f"ledger source {row['source_id']} has no target route")

    for failure in failures:
        print(failure)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
