#!/usr/bin/env python3
"""Validate the physical-line and error-tail contract of a V3 main log."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


OUTER_RE = re.compile(
    r"^\[[0-9]+\.[0-9]{6}\]\[tid [0-9]+\]\[[TDIWEF]\]"
    r"\[[^\]\r\n]+\] (?P<payload>.+)$"
)
NESTED_OUTER_RE = re.compile(
    r"\[[0-9]+\.[0-9]{6}\]\[tid [0-9]+\]\[[TDIWEF]\]"
    r"\[[^\]\r\n]+\] "
)
LEVEL_RE = re.compile(
    r"^\[[0-9]+\.[0-9]{6}\]\[tid [0-9]+\]\[(?P<level>[TDIWEF])\]"
)
ERROR_CODE_RE = re.compile(r" error_code=UGR-[0-9]{2}-[0-9]{2}$")
ANSI_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")


def validate(path: Path) -> list[str]:
    failures: list[str] = []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line:
            failures.append(f"line {number}: blank physical line")
            continue
        if ANSI_RE.search(line) or "\x00" in line or "\r" in line:
            failures.append(f"line {number}: forbidden control sequence")
        outer_match = OUTER_RE.fullmatch(line)
        if outer_match is None:
            failures.append(f"line {number}: invalid outer format")
            continue
        if NESTED_OUTER_RE.search(outer_match.group("payload")):
            failures.append(f"line {number}: nested V3 outer in payload")
        level = LEVEL_RE.match(line).group("level")
        codes = re.findall(r"error_code=UGR-[0-9]{2}-[0-9]{2}", line)
        if level in ("E", "F"):
            if len(codes) != 1 or not ERROR_CODE_RE.search(line):
                failures.append(f"line {number}: E/F requires exactly one trailing error code")
        elif codes:
            failures.append(f"line {number}: non-error line contains an error code")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    args = parser.parse_args()
    failures = validate(args.input)
    for failure in failures:
        print(failure)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
