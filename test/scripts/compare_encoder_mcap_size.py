#!/usr/bin/env python3

import argparse
import json
import struct
import tempfile
from pathlib import Path

from mcap.writer import CompressionType, Writer


OLD_SCHEMA = b'{"title":"UGripperEncoderBinary","description":"little-endian int32 raw + float32 rad"}'
DIRECT_STAMP_SCHEMA = b"""builtin_interfaces/Time stamp
int32 raw
float32 rad
================================================================================
MSG: builtin_interfaces/Time
int32 sec
uint32 nanosec"""
HEADER_SCHEMA = b"""std_msgs/Header header
int32 raw
float32 rad
================================================================================
MSG: std_msgs/Header
builtin_interfaces/Time stamp
string frame_id
================================================================================
MSG: builtin_interfaces/Time
int32 sec
uint32 nanosec"""


def encoder_direct_stamp_cdr(stamp_ns: int, raw: int, rad: float) -> bytes:
    sec, nanosec = divmod(stamp_ns, 1_000_000_000)
    return b"\x00\x01\x00\x00" + struct.pack("<iIif", sec, nanosec, raw, rad)


def encoder_header_cdr(stamp_ns: int, raw: int, rad: float) -> bytes:
    sec, nanosec = divmod(stamp_ns, 1_000_000_000)
    return (
        b"\x00\x01\x00\x00"
        + struct.pack("<iII", sec, nanosec, 1)
        + b"\x00\x00\x00\x00"
        + struct.pack("<if", raw, rad)
    )


def write_sample(path: Path, *, count_per_topic: int, mode: str) -> int:
    writer = Writer(str(path), compression=CompressionType.NONE)
    writer.start(profile="ros2", library="ugripper encoder size comparison")
    if mode == "header":
        schema_id = writer.register_schema("ugripper_msgs/msg/Encoder", "ros2msg", HEADER_SCHEMA)
        encoding = "cdr"
    elif mode == "direct_stamp":
        schema_id = writer.register_schema(
            "ugripper_msgs/msg/Encoder", "ros2msg", DIRECT_STAMP_SCHEMA
        )
        encoding = "cdr"
    elif mode == "legacy":
        schema_id = writer.register_schema("ugripper.Encoder", "jsonschema", OLD_SCHEMA)
        encoding = "binary"
    else:
        raise ValueError(f"unsupported mode: {mode}")
    channels = [
        writer.register_channel("/encoder_left", encoding, schema_id),
        writer.register_channel("/encoder_right", encoding, schema_id),
    ]
    start_ns = 1_700_000_000_000_000_000
    period_ns = 1_000_000
    for sequence in range(count_per_topic):
        stamp_ns = start_ns + sequence * period_ns
        for side, channel_id in enumerate(channels):
            raw = sequence * 2 + side
            rad = raw * 0.001
            if mode == "header":
                payload = encoder_header_cdr(stamp_ns, raw, rad)
            elif mode == "direct_stamp":
                payload = encoder_direct_stamp_cdr(stamp_ns, raw, rad)
            else:
                payload = struct.pack("<if", raw, rad)
            writer.add_message(channel_id, stamp_ns, payload, stamp_ns, sequence)
    writer.finish()
    return path.stat().st_size


def compare(count_per_topic: int, output_dir: Path) -> dict:
    old_path = output_dir / "encoder_legacy_binary.mcap"
    direct_path = output_dir / "encoder_ros2_direct_stamp_cdr.mcap"
    header_path = output_dir / "encoder_ros2_header_cdr.mcap"
    old_size = write_sample(old_path, count_per_topic=count_per_topic, mode="legacy")
    direct_size = write_sample(direct_path, count_per_topic=count_per_topic, mode="direct_stamp")
    header_size = write_sample(header_path, count_per_topic=count_per_topic, mode="header")
    total_messages = count_per_topic * 2
    return {
        "count_per_topic": count_per_topic,
        "total_messages": total_messages,
        "legacy_payload_bytes_per_message": 8,
        "direct_stamp_payload_bytes_per_message": 20,
        "header_payload_bytes_per_message": 28,
        "header_vs_direct_payload_delta_bytes_per_message": 8,
        "header_vs_legacy_payload_delta_bytes_per_message": 20,
        "legacy_file_bytes": old_size,
        "direct_stamp_file_bytes": direct_size,
        "header_file_bytes": header_size,
        "header_vs_direct_file_delta_bytes": header_size - direct_size,
        "header_vs_direct_file_delta_bytes_per_message": (
            header_size - direct_size
        ) / total_messages,
        "header_vs_legacy_file_delta_bytes": header_size - old_size,
        "projected_header_vs_direct_payload_delta_mb_per_hour_at_2000_hz": 57.6,
        "projected_header_vs_legacy_payload_delta_mb_per_hour_at_2000_hz": 144.0,
        "legacy_path": str(old_path),
        "direct_stamp_path": str(direct_path),
        "header_path": str(header_path),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare legacy and ROS 2 CDR encoder-only MCAP sizes.")
    parser.add_argument("--count-per-topic", type=int, default=10_000)
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    if args.count_per_topic <= 0:
        parser.error("--count-per-topic must be positive")

    if args.output_dir:
        args.output_dir.mkdir(parents=True, exist_ok=True)
        report = compare(args.count_per_topic, args.output_dir)
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        with tempfile.TemporaryDirectory(prefix="ugripper_encoder_size_") as temp_dir:
            report = compare(args.count_per_topic, Path(temp_dir))
            report.pop("legacy_path")
            report.pop("direct_stamp_path")
            report.pop("header_path")
            print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
