#!/usr/bin/env python3

import argparse
import json
import struct
import tempfile
from pathlib import Path

from mcap.writer import CompressionType, Writer


OLD_SCHEMA = b'{"title":"UGripperEncoderBinary","description":"little-endian int32 raw + float32 rad"}'
NEW_SCHEMA = b"""builtin_interfaces/Time stamp
int32 raw
float32 rad
================================================================================
MSG: builtin_interfaces/Time
int32 sec
uint32 nanosec"""


def encoder_cdr(stamp_ns: int, raw: int, rad: float) -> bytes:
    sec, nanosec = divmod(stamp_ns, 1_000_000_000)
    return b"\x00\x01\x00\x00" + struct.pack("<iIif", sec, nanosec, raw, rad)


def write_sample(path: Path, *, count_per_topic: int, cdr: bool) -> int:
    writer = Writer(str(path), compression=CompressionType.NONE)
    writer.start(profile="ros2", library="ugripper encoder size comparison")
    if cdr:
        schema_id = writer.register_schema("ugripper_msgs/msg/Encoder", "ros2msg", NEW_SCHEMA)
        encoding = "cdr"
    else:
        schema_id = writer.register_schema("ugripper.Encoder", "jsonschema", OLD_SCHEMA)
        encoding = "binary"
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
            payload = encoder_cdr(stamp_ns, raw, rad) if cdr else struct.pack("<if", raw, rad)
            writer.add_message(channel_id, stamp_ns, payload, stamp_ns, sequence)
    writer.finish()
    return path.stat().st_size


def compare(count_per_topic: int, output_dir: Path) -> dict:
    old_path = output_dir / "encoder_legacy_binary.mcap"
    new_path = output_dir / "encoder_ros2_cdr.mcap"
    old_size = write_sample(old_path, count_per_topic=count_per_topic, cdr=False)
    new_size = write_sample(new_path, count_per_topic=count_per_topic, cdr=True)
    total_messages = count_per_topic * 2
    return {
        "count_per_topic": count_per_topic,
        "total_messages": total_messages,
        "old_payload_bytes_per_message": 8,
        "new_payload_bytes_per_message": 20,
        "payload_delta_bytes_per_message": 12,
        "old_file_bytes": old_size,
        "new_file_bytes": new_size,
        "file_delta_bytes": new_size - old_size,
        "file_delta_bytes_per_message": (new_size - old_size) / total_messages,
        "projected_payload_delta_bytes_per_second_at_2000_hz": 24_000,
        "projected_payload_delta_mb_per_hour_at_2000_hz": 86.4,
        "old_path": str(old_path),
        "new_path": str(new_path),
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
            report.pop("old_path")
            report.pop("new_path")
            print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
