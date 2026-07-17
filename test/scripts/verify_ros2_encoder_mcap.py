#!/usr/bin/env python3

import argparse
import json
import math
from pathlib import Path

from mcap.reader import make_reader
from mcap.writer import CompressionType, Writer
from rclpy.serialization import deserialize_message
from std_msgs.msg import String
from ugripper_msgs.msg import Encoder


ENCODER_SCHEMA = "ugripper_msgs/msg/Encoder"
ENCODER_TOPICS = {"/encoder_left", "/encoder_right"}
JSON_STRING_TOPICS = {"/calibration", "/metadata"}


def stamp_ns(message: Encoder) -> int:
    return message.header.stamp.sec * 1_000_000_000 + message.header.stamp.nanosec


def verify(input_path: Path, filtered_output: Path | None) -> dict:
    stats = {
        topic: {
            "count": 0,
            "first_log_time_ns": None,
            "last_log_time_ns": None,
            "max_gap_ns": 0,
            "timestamp_regressions": 0,
            "stamp_log_mismatches": 0,
            "min_payload_bytes": None,
            "max_payload_bytes": 0,
            "first_value": None,
            "last_value": None,
        }
        for topic in sorted(ENCODER_TOPICS)
    }
    output_writer = None
    output_schemas = {}
    output_channels = {}
    json_topics = {
        topic: {"count": 0, "valid_json": 0, "content_type": None, "ugripper_json_kind": None}
        for topic in sorted(JSON_STRING_TOPICS)
    }

    with input_path.open("rb") as stream:
        header = make_reader(stream).get_header()
    if header.profile != "ros2":
        raise ValueError(f"expected ros2 profile, got {header.profile!r}")

    if filtered_output is not None:
        filtered_output.parent.mkdir(parents=True, exist_ok=True)
        output_writer = Writer(str(filtered_output), compression=CompressionType.NONE)
        output_writer.start(profile="ros2", library="ugripper ROS 2 encoder verification")

    try:
        with input_path.open("rb") as stream:
            for schema, channel, record in make_reader(stream).iter_messages(topics=ENCODER_TOPICS):
                if schema is None or schema.name != ENCODER_SCHEMA:
                    raise ValueError(f"{channel.topic}: unexpected schema {getattr(schema, 'name', None)!r}")
                if schema.encoding != "ros2msg" or channel.message_encoding != "cdr":
                    raise ValueError(
                        f"{channel.topic}: expected ros2msg/cdr, got {schema.encoding}/{channel.message_encoding}"
                    )

                decoded = deserialize_message(record.data, Encoder)
                decoded_stamp_ns = stamp_ns(decoded)
                if not math.isfinite(decoded.rad):
                    raise ValueError(f"{channel.topic}: non-finite rad value")
                if decoded.header.frame_id:
                    raise ValueError(
                        f"{channel.topic}: expected empty frame_id, got {decoded.header.frame_id!r}"
                    )

                topic_stats = stats[channel.topic]
                previous = topic_stats["last_log_time_ns"]
                if previous is not None:
                    if record.log_time < previous:
                        topic_stats["timestamp_regressions"] += 1
                    else:
                        topic_stats["max_gap_ns"] = max(
                            topic_stats["max_gap_ns"], record.log_time - previous
                        )
                if decoded_stamp_ns != record.log_time:
                    topic_stats["stamp_log_mismatches"] += 1
                value = {"stamp_ns": decoded_stamp_ns, "raw": decoded.raw, "rad": decoded.rad}
                if topic_stats["count"] == 0:
                    topic_stats["first_log_time_ns"] = record.log_time
                    topic_stats["first_value"] = value
                topic_stats["last_log_time_ns"] = record.log_time
                topic_stats["last_value"] = value
                topic_stats["count"] += 1
                payload_size = len(record.data)
                topic_stats["min_payload_bytes"] = (
                    payload_size
                    if topic_stats["min_payload_bytes"] is None
                    else min(topic_stats["min_payload_bytes"], payload_size)
                )
                topic_stats["max_payload_bytes"] = max(topic_stats["max_payload_bytes"], payload_size)

                if output_writer is not None:
                    schema_key = (schema.name, schema.encoding, bytes(schema.data))
                    output_schema_id = output_schemas.get(schema_key)
                    if output_schema_id is None:
                        output_schema_id = output_writer.register_schema(
                            schema.name, schema.encoding, bytes(schema.data)
                        )
                        output_schemas[schema_key] = output_schema_id
                    output_channel_id = output_channels.get(channel.topic)
                    if output_channel_id is None:
                        output_channel_id = output_writer.register_channel(
                            channel.topic,
                            channel.message_encoding,
                            output_schema_id,
                            dict(channel.metadata or {}),
                        )
                        output_channels[channel.topic] = output_channel_id
                    output_writer.add_message(
                        output_channel_id,
                        record.log_time,
                        bytes(record.data),
                        record.publish_time,
                        record.sequence,
                    )
    finally:
        if output_writer is not None:
            output_writer.finish()

    for topic, topic_stats in stats.items():
        if topic_stats["count"] == 0:
            raise ValueError(f"missing encoder topic: {topic}")
        if topic_stats["timestamp_regressions"] or topic_stats["stamp_log_mismatches"]:
            raise ValueError(f"{topic}: timestamp validation failed: {topic_stats}")

    with input_path.open("rb") as stream:
        for schema, channel, record in make_reader(stream).iter_messages(topics=JSON_STRING_TOPICS):
            if schema is None or schema.name != "std_msgs/msg/String":
                raise ValueError(f"{channel.topic}: unexpected schema {getattr(schema, 'name', None)!r}")
            if schema.encoding != "ros2msg" or channel.message_encoding != "cdr":
                raise ValueError(
                    f"{channel.topic}: expected ros2msg/cdr, got {schema.encoding}/{channel.message_encoding}"
                )
            decoded = deserialize_message(record.data, String)
            json.loads(decoded.data)
            topic_stats = json_topics[channel.topic]
            topic_stats["count"] += 1
            topic_stats["valid_json"] += 1
            topic_stats["content_type"] = (channel.metadata or {}).get("content_type")
            topic_stats["ugripper_json_kind"] = (channel.metadata or {}).get("ugripper_json_kind")

    if json_topics["/calibration"]["count"] == 0:
        raise ValueError("missing /calibration std_msgs/msg/String topic")

    return {
        "input": str(input_path),
        "profile": header.profile,
        "filtered_output": str(filtered_output) if filtered_output else None,
        "topics": stats,
        "json_topics": json_topics,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Deserialize UGripper encoder CDR with the installed ROS 2 message type."
    )
    parser.add_argument("input", type=Path)
    parser.add_argument("--filtered-output", type=Path)
    args = parser.parse_args()
    print(json.dumps(verify(args.input, args.filtered_output), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
