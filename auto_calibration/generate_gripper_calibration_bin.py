#!/usr/bin/env python3
"""Generate the 1024-byte HMI force calibration payload."""

from __future__ import annotations

import argparse
import struct
import time
import zlib
from pathlib import Path


PAYLOAD_SIZE = 1024
HEADER_SIZE = 32
FORCE_BLOCK_SIZE = 64
DATA_FORMAT_VERSION = 0x00020000
VALID_FORCE_SENSOR = 1 << 0
FORCE_CALIBRATION_VALID = 1 << 0


def encode_serial_number(value: str) -> bytes:
    encoded = value.encode("ascii")
    if not encoded or len(encoded) > 32 or any(byte < 0x20 or byte > 0x7E for byte in encoded):
        raise ValueError("SN must contain 1..32 printable ASCII characters")
    return encoded.ljust(32, b"\x00")


def build_payload(
    serial_number: str,
    zero_raw: int,
    reference_raw: int,
    reference_mass_mg: int,
    calibrated_at_unix_s: int,
) -> bytes:
    if not -(1 << 31) <= zero_raw < (1 << 31):
        raise ValueError("zero raw is outside signed int32")
    if not -(1 << 31) <= reference_raw < (1 << 31):
        raise ValueError("reference raw is outside signed int32")
    if reference_raw == zero_raw:
        raise ValueError("reference raw must differ from zero raw")
    if not 0 < reference_mass_mg < (1 << 32):
        raise ValueError("reference mass must fit positive uint32 milligrams")

    serial = encode_serial_number(serial_number)
    header = struct.pack(
        "<4sIHHI16s",
        b"UCAL",
        DATA_FORMAT_VERSION,
        HEADER_SIZE + FORCE_BLOCK_SIZE,
        HEADER_SIZE,
        VALID_FORCE_SENSOR,
        b"\x00" * 16,
    )
    block_without_crc = struct.pack(
        "<4sHH32siiIII",
        b"FCAL",
        1,
        FORCE_BLOCK_SIZE,
        serial,
        zero_raw,
        reference_raw,
        reference_mass_mg,
        FORCE_CALIBRATION_VALID,
        calibrated_at_unix_s,
    )
    block = block_without_crc + struct.pack("<I", zlib.crc32(block_without_crc) & 0xFFFFFFFF)
    payload = header + block
    return payload.ljust(PAYLOAD_SIZE, b"\x00")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate an SN-bound HMI force calibration payload")
    parser.add_argument("--sn", required=True, help="existing gripper SN, encoded as the same 32-byte HMI field")
    parser.add_argument("--zero-raw", required=True, type=int)
    parser.add_argument("--reference-raw", required=True, type=int)
    parser.add_argument("--reference-mass-mg", required=True, type=int)
    parser.add_argument("--calibrated-at-unix-s", type=int, default=int(time.time()))
    parser.add_argument("--output-bin", required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    payload = build_payload(
        args.sn,
        args.zero_raw,
        args.reference_raw,
        args.reference_mass_mg,
        args.calibrated_at_unix_s,
    )
    output = Path(args.output_bin)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(payload)
    print(
        f"generated force calibration payload: {output} size={len(payload)} "
        f"sn={args.sn} zero_raw={args.zero_raw} reference_raw={args.reference_raw} "
        f"reference_mass_mg={args.reference_mass_mg}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
