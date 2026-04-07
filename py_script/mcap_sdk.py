#!/usr/bin/env python3
"""
Customer-facing MCAP parsing SDK and CLI for ugripper datasets.

This module focuses on data consumption only:
- inspect MCAP file metadata and topics
- iterate decoded messages
- export decoded messages to CSV

Supported ugripper topics:
- fays_data.mcap: "i", "c"
- sensor_data.mcap: "imu_raw", "encoder"
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import struct
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Dict, Iterable, Iterator, List, Optional, Set, Tuple, Union

from mcap.reader import make_reader
from mcap.stream_reader import StreamReader


def ns_to_iso8601(timestamp_ns: Optional[int]) -> str:
    if timestamp_ns is None:
        return ""
    whole_seconds = timestamp_ns // 1_000_000_000
    nanoseconds = timestamp_ns % 1_000_000_000
    base = dt.datetime.fromtimestamp(whole_seconds, tz=dt.timezone.utc)
    return f"{base.strftime('%Y-%m-%dT%H:%M:%S')}.{nanoseconds:09d}Z"


def normalize_topics(topics: Optional[Iterable[str]]) -> Optional[Set[str]]:
    if topics is None:
        return None
    normalized = {topic for topic in topics if topic}
    return normalized or None


def sanitize_topic_for_filename(topic: str) -> str:
    sanitized = []
    for ch in topic:
        sanitized.append(ch if ch.isalnum() or ch in {"-", "_"} else "_")
    return "".join(sanitized).strip("_") or "topic"


def resolve_message_encoding(schema: Any, channel: Any) -> str:
    channel_encoding = getattr(channel, "message_encoding", None) or ""
    schema_encoding = getattr(schema, "encoding", None) if schema else ""
    return channel_encoding or schema_encoding or ""


def decode_fays_imu(payload: bytes) -> Dict[str, Any]:
    if len(payload) >= 48:
        gx, gy, gz, ax, ay, az = struct.unpack("<6d", payload[:48])
        return {
            "payload_format": "float64x6",
            "gx": gx,
            "gy": gy,
            "gz": gz,
            "ax": ax,
            "ay": ay,
            "az": az,
        }
    if len(payload) >= 24:
        gx, gy, gz, ax, ay, az = struct.unpack("<6f", payload[:24])
        return {
            "payload_format": "float32x6_legacy",
            "gx": gx,
            "gy": gy,
            "gz": gz,
            "ax": ax,
            "ay": ay,
            "az": az,
        }
    raise ValueError(f"topic 'i' payload too short: {len(payload)} bytes")


def decode_fays_cam(payload: bytes) -> Dict[str, Any]:
    if len(payload) < 4:
        raise ValueError(f"topic 'c' payload too short: {len(payload)} bytes")
    (frame_index,) = struct.unpack("<I", payload[:4])
    return {"frame_index": frame_index}


def decode_sensor_imu(payload: bytes) -> Dict[str, Any]:
    if len(payload) < 40:
        raise ValueError(f"topic 'imu_raw' payload too short: {len(payload)} bytes")
    qx, qy, qz, qw, gx, gy, gz, ax, ay, az = struct.unpack("<10f", payload[:40])
    return {
        "qx": qx,
        "qy": qy,
        "qz": qz,
        "qw": qw,
        "gx": gx,
        "gy": gy,
        "gz": gz,
        "ax": ax,
        "ay": ay,
        "az": az,
    }


def decode_encoder(payload: bytes) -> Dict[str, Any]:
    if len(payload) < 8:
        raise ValueError(f"topic 'encoder' payload too short: {len(payload)} bytes")
    raw, rad = struct.unpack("<if", payload[:8])
    return {"raw": raw, "rad": rad}


def decode_unknown_payload(payload: bytes) -> Dict[str, Any]:
    return {
        "payload_hex": payload.hex(),
    }


@dataclass(frozen=True)
class TopicDefinition:
    topic: str
    display_topic: str
    dataset: str
    description: str
    payload_description: str
    preferred_time_field: str
    decoder: Callable[[bytes], Dict[str, Any]]
    csv_columns: Tuple[str, ...]
    aliases: Tuple[str, ...] = ()


TOPIC_DEFINITIONS: Dict[str, TopicDefinition] = {
    "i": TopicDefinition(
        topic="i",
        display_topic="fays_imu",
        dataset="fays_data.mcap",
        description="Fays IMU samples",
        payload_description="little-endian float64[6]: gx, gy, gz, ax, ay, az",
        preferred_time_field="publish_time_ns",
        decoder=decode_fays_imu,
        csv_columns=("payload_format", "gx", "gy", "gz", "ax", "ay", "az"),
        aliases=("fays_imu",),
    ),
    "c": TopicDefinition(
        topic="c",
        display_topic="fays_frame_index",
        dataset="fays_data.mcap",
        description="Fays video frame timestamp index",
        payload_description="little-endian uint32: frame_index",
        preferred_time_field="publish_time_ns",
        decoder=decode_fays_cam,
        csv_columns=("frame_index",),
        aliases=("fays_frame_index", "fays_frame"),
    ),
    "imu_raw": TopicDefinition(
        topic="imu_raw",
        display_topic="imu_raw",
        dataset="sensor_data.mcap",
        description="Body IMU samples",
        payload_description="little-endian float32[10]: qx, qy, qz, qw, gx, gy, gz, ax, ay, az",
        preferred_time_field="log_time_ns",
        decoder=decode_sensor_imu,
        csv_columns=("qx", "qy", "qz", "qw", "gx", "gy", "gz", "ax", "ay", "az"),
        aliases=("imu_raw",),
    ),
    "encoder": TopicDefinition(
        topic="encoder",
        display_topic="encoder",
        dataset="sensor_data.mcap",
        description="Encoder position samples",
        payload_description="little-endian int32 raw, float32 rad",
        preferred_time_field="log_time_ns",
        decoder=decode_encoder,
        csv_columns=("raw", "rad"),
        aliases=("encoder",),
    ),
}


COMMON_CSV_COLUMNS = (
    "topic",
    "raw_topic",
    "dataset",
    "schema_name",
    "message_encoding",
    "sequence",
    "payload_size",
    "log_time_ns",
    "log_time_iso",
    "publish_time_ns",
    "publish_time_iso",
    "publish_minus_log_ns",
    "publish_minus_log_ms",
    "recommended_time_field",
    "recommended_time_ns",
    "recommended_time_iso",
)

DEFAULT_TOPIC_ALIASES = {
    "fays_imu": "fays_imu",
    "fays_frame_index": "fays_frame_index",
    "imu_raw": "imu_raw",
    "encoder": "encoder",
}


TOPIC_NAME_TO_RAW: Dict[str, str] = {}
for raw_topic, definition in TOPIC_DEFINITIONS.items():
    TOPIC_NAME_TO_RAW[raw_topic] = raw_topic
    TOPIC_NAME_TO_RAW[definition.display_topic] = raw_topic
    for alias in definition.aliases:
        TOPIC_NAME_TO_RAW[alias] = raw_topic


def resolve_topic_filters(topics: Optional[Iterable[str]]) -> Optional[Set[str]]:
    normalized = normalize_topics(topics)
    if normalized is None:
        return None
    resolved = set()
    for topic in normalized:
        resolved.add(TOPIC_NAME_TO_RAW.get(topic, topic))
    return resolved


class McapParser:
    def __init__(self, mcap_path: Union[str, Path]):
        self.path = Path(mcap_path)
        if not self.path.is_file():
            raise FileNotFoundError(f"MCAP file not found: {self.path}")
        self._summary_cache: Any = None
        self._summary_loaded = False

    def get_summary(self) -> Any:
        if self._summary_loaded:
            return self._summary_cache
        summary = None
        try:
            with self.path.open("rb") as file_obj:
                summary = make_reader(file_obj).get_summary()
        except Exception:
            summary = None
        self._summary_cache = summary
        self._summary_loaded = True
        return summary

    def describe(self) -> Dict[str, Any]:
        summary = self.get_summary()
        info: Dict[str, Any] = {
            "path": str(self.path.resolve()),
            "file_name": self.path.name,
            "size_bytes": self.path.stat().st_size,
            "has_summary": summary is not None,
            "topics": [],
        }

        if (
            summary is not None
            and summary.statistics is not None
            and (summary.channels or {})
        ):
            stats = summary.statistics
            info["message_count"] = int(stats.message_count)
            info["message_start_time_ns"] = int(stats.message_start_time)
            info["message_end_time_ns"] = int(stats.message_end_time)
            info["message_start_time_iso"] = ns_to_iso8601(int(stats.message_start_time))
            info["message_end_time_iso"] = ns_to_iso8601(int(stats.message_end_time))
            info["span_seconds"] = max(
                0.0,
                (int(stats.message_end_time) - int(stats.message_start_time)) / 1_000_000_000,
            )

            topic_rows: List[Dict[str, Any]] = []
            message_counts = stats.channel_message_counts or {}
            for channel_id, channel in sorted((summary.channels or {}).items(), key=lambda item: item[1].topic):
                schema = (summary.schemas or {}).get(channel.schema_id)
                definition = TOPIC_DEFINITIONS.get(channel.topic)
                topic_rows.append(
                    {
                        "topic": definition.display_topic if definition else channel.topic,
                        "raw_topic": channel.topic,
                        "schema_name": getattr(schema, "name", None),
                        "message_encoding": resolve_message_encoding(schema, channel),
                        "message_count": int(message_counts.get(channel_id, 0)),
                        "description": definition.description if definition else "Unknown topic",
                        "payload_description": (
                            definition.payload_description if definition else "Unknown payload"
                        ),
                        "recommended_time_field": (
                            definition.preferred_time_field if definition else "log_time_ns"
                        ),
                    }
                )
            info["topics"] = topic_rows
            return info

        topic_stats: Dict[str, Dict[str, Any]] = {}
        message_count = 0
        first_ts: Optional[int] = None
        last_ts: Optional[int] = None

        for record in self.iter_decoded_messages():
            message_count += 1
            log_time_ns = record["log_time_ns"]
            if first_ts is None:
                first_ts = log_time_ns
            last_ts = log_time_ns
            topic = record["topic"]
            topic_stat = topic_stats.setdefault(
                topic,
                {
                    "topic": topic,
                    "raw_topic": record["raw_topic"],
                    "schema_name": record["schema_name"],
                    "message_encoding": record["message_encoding"],
                    "message_count": 0,
                    "description": record["description"],
                    "payload_description": record["payload_description"],
                    "recommended_time_field": record["recommended_time_field"],
                },
            )
            topic_stat["message_count"] += 1

        info["message_count"] = message_count
        info["message_start_time_ns"] = first_ts
        info["message_end_time_ns"] = last_ts
        info["message_start_time_iso"] = ns_to_iso8601(first_ts)
        info["message_end_time_iso"] = ns_to_iso8601(last_ts)
        info["span_seconds"] = (
            0.0 if first_ts is None or last_ts is None else (last_ts - first_ts) / 1_000_000_000
        )
        info["topics"] = [topic_stats[key] for key in sorted(topic_stats)]
        return info

    def get_info(self) -> Dict[str, Any]:
        return self.describe()

    def list_topics(self) -> List[Dict[str, Any]]:
        return list(self.get_info().get("topics", []))

    def iter_messages(
        self,
        topics: Optional[Iterable[str]] = None,
        start_time_ns: Optional[int] = None,
        end_time_ns: Optional[int] = None,
        reverse: bool = False,
    ) -> Iterator[Tuple[Any, Any, Any]]:
        topics_set = resolve_topic_filters(topics)
        summary = self.get_summary()

        if summary is not None:
            yielded = 0
            buffered_items = []
            with self.path.open("rb") as file_obj:
                reader = make_reader(file_obj)
                for item in reader.iter_messages(
                    topics=topics_set,
                    start_time=start_time_ns,
                    end_time=end_time_ns,
                    reverse=reverse,
                ):
                    yielded += 1
                    if reverse:
                        buffered_items.append(item)
                    else:
                        yield item
            if yielded > 0:
                if reverse:
                    for item in reversed(buffered_items):
                        yield item
                return

        buffered_items: List[Tuple[Any, Any, Any]] = []
        yielded = 0
        with self.path.open("rb") as file_obj:
            reader = make_reader(file_obj)
            for schema, channel, message in reader.iter_messages(
                topics=topics_set,
                log_time_order=False,
            ):
                yielded += 1
                if start_time_ns is not None and int(message.log_time) < start_time_ns:
                    continue
                if end_time_ns is not None and int(message.log_time) >= end_time_ns:
                    continue
                item = (schema, channel, message)
                if reverse:
                    buffered_items.append(item)
                else:
                    yield item

        if yielded > 0:
            if reverse:
                for item in reversed(buffered_items):
                    yield item
            return

        schemas: Dict[int, Any] = {}
        channels: Dict[int, Any] = {}
        buffered_items = []
        with self.path.open("rb") as file_obj:
            for record in StreamReader(file_obj).records:
                record_name = type(record).__name__
                if record_name == "Schema":
                    schemas[record.id] = record
                    continue
                if record_name == "Channel":
                    channels[record.id] = record
                    continue
                if record_name != "Message":
                    continue

                channel = channels.get(record.channel_id)
                if channel is None:
                    continue
                if topics_set and channel.topic not in topics_set:
                    continue
                if start_time_ns is not None and int(record.log_time) < start_time_ns:
                    continue
                if end_time_ns is not None and int(record.log_time) >= end_time_ns:
                    continue

                item = (schemas.get(channel.schema_id), channel, record)
                if reverse:
                    buffered_items.append(item)
                else:
                    yield item

        if reverse:
            for item in reversed(buffered_items):
                yield item

    def decode_message(self, schema: Any, channel: Any, message: Any) -> Dict[str, Any]:
        topic = channel.topic
        definition = TOPIC_DEFINITIONS.get(topic)
        decoder = definition.decoder if definition else decode_unknown_payload
        decoded = decoder(bytes(message.data))

        log_time_ns = int(message.log_time)
        publish_time_ns = int(getattr(message, "publish_time", 0) or 0)
        publish_minus_log_ns = publish_time_ns - log_time_ns if publish_time_ns else 0

        if definition is not None and definition.preferred_time_field == "publish_time_ns" and publish_time_ns:
            recommended_time_ns = publish_time_ns
        else:
            recommended_time_ns = log_time_ns

        return {
            "topic": definition.display_topic if definition else topic,
            "raw_topic": topic,
            "dataset": definition.dataset if definition else self.path.name,
            "description": definition.description if definition else "Unknown topic",
            "payload_description": (
                definition.payload_description if definition else "Unknown payload"
            ),
            "schema_name": getattr(schema, "name", "") if schema else "",
            "message_encoding": resolve_message_encoding(schema, channel),
            "sequence": int(getattr(message, "sequence", 0)),
            "payload_size": len(message.data),
            "log_time_ns": log_time_ns,
            "log_time_iso": ns_to_iso8601(log_time_ns),
            "publish_time_ns": publish_time_ns,
            "publish_time_iso": ns_to_iso8601(publish_time_ns) if publish_time_ns else "",
            "publish_minus_log_ns": publish_minus_log_ns,
            "publish_minus_log_ms": publish_minus_log_ns / 1_000_000,
            "recommended_time_field": (
                definition.preferred_time_field if definition else "log_time_ns"
            ),
            "recommended_time_ns": recommended_time_ns,
            "recommended_time_iso": ns_to_iso8601(recommended_time_ns),
            **decoded,
        }

    def iter_decoded_messages(
        self,
        topics: Optional[Iterable[str]] = None,
        start_time_ns: Optional[int] = None,
        end_time_ns: Optional[int] = None,
        reverse: bool = False,
    ) -> Iterator[Dict[str, Any]]:
        for schema, channel, message in self.iter_messages(
            topics=topics,
            start_time_ns=start_time_ns,
            end_time_ns=end_time_ns,
            reverse=reverse,
        ):
            yield self.decode_message(schema, channel, message)

    def iter_records(
        self,
        topics: Optional[Iterable[str]] = None,
        start_time_ns: Optional[int] = None,
        end_time_ns: Optional[int] = None,
        reverse: bool = False,
    ) -> Iterator[Dict[str, Any]]:
        return self.iter_decoded_messages(
            topics=topics,
            start_time_ns=start_time_ns,
            end_time_ns=end_time_ns,
            reverse=reverse,
        )

    def read_messages(
        self,
        topics: Optional[Iterable[str]] = None,
        start_time_ns: Optional[int] = None,
        end_time_ns: Optional[int] = None,
        reverse: bool = False,
        limit: Optional[int] = None,
    ) -> List[Dict[str, Any]]:
        rows: List[Dict[str, Any]] = []
        for row in self.iter_decoded_messages(
            topics=topics,
            start_time_ns=start_time_ns,
            end_time_ns=end_time_ns,
            reverse=reverse,
        ):
            rows.append(row)
            if limit is not None and len(rows) >= limit:
                break
        return rows

    def read_topic(
        self,
        topic: str,
        start_time_ns: Optional[int] = None,
        end_time_ns: Optional[int] = None,
        reverse: bool = False,
        limit: Optional[int] = None,
    ) -> List[Dict[str, Any]]:
        return self.read_messages(
            topics=[topic],
            start_time_ns=start_time_ns,
            end_time_ns=end_time_ns,
            reverse=reverse,
            limit=limit,
        )

    def topic_csv_columns(self, topic: str, sample_row: Dict[str, Any]) -> List[str]:
        definition = TOPIC_DEFINITIONS.get(sample_row.get("raw_topic", topic))
        columns = list(COMMON_CSV_COLUMNS)
        if definition is not None:
            columns.extend(definition.csv_columns)
        else:
            extra_keys = sorted(key for key in sample_row if key not in COMMON_CSV_COLUMNS)
            columns.extend(extra_keys)
        return columns

    def export_csv(
        self,
        output_dir: Union[str, Path],
        topics: Optional[Iterable[str]] = None,
        start_time_ns: Optional[int] = None,
        end_time_ns: Optional[int] = None,
    ) -> Dict[str, Any]:
        output_root = Path(output_dir)
        output_root.mkdir(parents=True, exist_ok=True)

        writers: Dict[str, csv.DictWriter] = {}
        handles: Dict[str, Any] = {}
        output_paths: Dict[str, Path] = {}
        row_counts: Dict[str, int] = defaultdict(int)

        try:
            for row in self.iter_decoded_messages(
                topics=topics,
                start_time_ns=start_time_ns,
                end_time_ns=end_time_ns,
            ):
                topic = row["topic"]
                if topic not in writers:
                    filename = f"{self.path.stem}__{sanitize_topic_for_filename(topic)}.csv"
                    output_path = output_root / filename
                    handle = output_path.open("w", newline="", encoding="utf-8")
                    fieldnames = self.topic_csv_columns(topic, row)
                    writer = csv.DictWriter(handle, fieldnames=fieldnames, extrasaction="ignore")
                    writer.writeheader()
                    writers[topic] = writer
                    handles[topic] = handle
                    output_paths[topic] = output_path

                writers[topic].writerow(row)
                row_counts[topic] += 1
        finally:
            for handle in handles.values():
                handle.close()

        return {
            "output_dir": str(output_root.resolve()),
            "files": {
                topic: {
                    "path": str(path.resolve()),
                    "rows": row_counts.get(topic, 0),
                }
                for topic, path in sorted(output_paths.items())
            },
        }


class FaysMcapReader(McapParser):
    """Convenience reader for fays_data.mcap."""

    def iter_imu(
        self,
        start_time_ns: Optional[int] = None,
        end_time_ns: Optional[int] = None,
        reverse: bool = False,
    ) -> Iterator[Dict[str, Any]]:
        return self.iter_records(
            topics=[DEFAULT_TOPIC_ALIASES["fays_imu"]],
            start_time_ns=start_time_ns,
            end_time_ns=end_time_ns,
            reverse=reverse,
        )

    def read_imu(
        self,
        start_time_ns: Optional[int] = None,
        end_time_ns: Optional[int] = None,
        reverse: bool = False,
        limit: Optional[int] = None,
    ) -> List[Dict[str, Any]]:
        return self.read_topic(
            DEFAULT_TOPIC_ALIASES["fays_imu"],
            start_time_ns=start_time_ns,
            end_time_ns=end_time_ns,
            reverse=reverse,
            limit=limit,
        )

    def iter_frame_index(
        self,
        start_time_ns: Optional[int] = None,
        end_time_ns: Optional[int] = None,
        reverse: bool = False,
    ) -> Iterator[Dict[str, Any]]:
        return self.iter_records(
            topics=[DEFAULT_TOPIC_ALIASES["fays_frame_index"]],
            start_time_ns=start_time_ns,
            end_time_ns=end_time_ns,
            reverse=reverse,
        )

    def read_frame_index(
        self,
        start_time_ns: Optional[int] = None,
        end_time_ns: Optional[int] = None,
        reverse: bool = False,
        limit: Optional[int] = None,
    ) -> List[Dict[str, Any]]:
        return self.read_topic(
            DEFAULT_TOPIC_ALIASES["fays_frame_index"],
            start_time_ns=start_time_ns,
            end_time_ns=end_time_ns,
            reverse=reverse,
            limit=limit,
        )

    def build_frame_index_map(
        self,
        time_field: str = "recommended_time_ns",
    ) -> Dict[int, int]:
        mapping: Dict[int, int] = {}
        for row in self.iter_frame_index():
            mapping[int(row["frame_index"])] = int(row[time_field])
        return mapping


class SensorMcapReader(McapParser):
    """Convenience reader for sensor_data.mcap."""

    def iter_imu(
        self,
        start_time_ns: Optional[int] = None,
        end_time_ns: Optional[int] = None,
        reverse: bool = False,
    ) -> Iterator[Dict[str, Any]]:
        return self.iter_records(
            topics=[DEFAULT_TOPIC_ALIASES["imu_raw"]],
            start_time_ns=start_time_ns,
            end_time_ns=end_time_ns,
            reverse=reverse,
        )

    def read_imu(
        self,
        start_time_ns: Optional[int] = None,
        end_time_ns: Optional[int] = None,
        reverse: bool = False,
        limit: Optional[int] = None,
    ) -> List[Dict[str, Any]]:
        return self.read_topic(
            DEFAULT_TOPIC_ALIASES["imu_raw"],
            start_time_ns=start_time_ns,
            end_time_ns=end_time_ns,
            reverse=reverse,
            limit=limit,
        )

    def iter_encoder(
        self,
        start_time_ns: Optional[int] = None,
        end_time_ns: Optional[int] = None,
        reverse: bool = False,
    ) -> Iterator[Dict[str, Any]]:
        return self.iter_records(
            topics=[DEFAULT_TOPIC_ALIASES["encoder"]],
            start_time_ns=start_time_ns,
            end_time_ns=end_time_ns,
            reverse=reverse,
        )

    def read_encoder(
        self,
        start_time_ns: Optional[int] = None,
        end_time_ns: Optional[int] = None,
        reverse: bool = False,
        limit: Optional[int] = None,
    ) -> List[Dict[str, Any]]:
        return self.read_topic(
            DEFAULT_TOPIC_ALIASES["encoder"],
            start_time_ns=start_time_ns,
            end_time_ns=end_time_ns,
            reverse=reverse,
            limit=limit,
        )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="ugripper customer-facing MCAP parser SDK and CSV export tool.",
    )

    subparsers = parser.add_subparsers(dest="command", required=True)

    info_parser = subparsers.add_parser(
        "info",
        help="Print MCAP file summary and topic definitions.",
    )
    info_parser.add_argument("mcap", help="Path to an MCAP file.")

    show_parser = subparsers.add_parser(
        "show",
        help="Print decoded messages as JSON lines.",
    )
    show_parser.add_argument("mcap", help="Path to an MCAP file.")
    show_parser.add_argument(
        "-t",
        "--topic",
        action="append",
        help="Topic to include. Supports raw names like i/c and readable names like fays_imu.",
    )
    show_parser.add_argument(
        "--start-ns",
        type=int,
        help="Inclusive log_time lower bound in nanoseconds.",
    )
    show_parser.add_argument(
        "--end-ns",
        type=int,
        help="Exclusive log_time upper bound in nanoseconds.",
    )
    show_parser.add_argument(
        "--limit",
        type=int,
        default=20,
        help="Maximum number of decoded messages to print. Default: 20.",
    )
    show_parser.add_argument(
        "--reverse",
        action="store_true",
        help="Read newest matching messages first. Fastest when MCAP summary exists.",
    )
    show_parser.add_argument(
        "--pretty",
        action="store_true",
        help="Pretty-print JSON output.",
    )

    csv_parser = subparsers.add_parser(
        "export-csv",
        help="Export decoded messages to per-topic CSV files.",
    )
    csv_parser.add_argument("mcap", help="Path to an MCAP file.")
    csv_parser.add_argument(
        "-o",
        "--output-dir",
        required=True,
        help="Directory for generated CSV files.",
    )
    csv_parser.add_argument(
        "-t",
        "--topic",
        action="append",
        help="Topic to export. Supports raw names like i/c and readable names like fays_imu.",
    )
    csv_parser.add_argument(
        "--start-ns",
        type=int,
        help="Inclusive log_time lower bound in nanoseconds.",
    )
    csv_parser.add_argument(
        "--end-ns",
        type=int,
        help="Exclusive log_time upper bound in nanoseconds.",
    )

    return parser


def cmd_info(args: argparse.Namespace) -> int:
    parser = McapParser(args.mcap)
    info = parser.describe()
    print(json.dumps(info, ensure_ascii=False, indent=2))
    return 0


def cmd_show(args: argparse.Namespace) -> int:
    parser = McapParser(args.mcap)
    count = 0
    for row in parser.iter_decoded_messages(
        topics=args.topic,
        start_time_ns=args.start_ns,
        end_time_ns=args.end_ns,
        reverse=args.reverse,
    ):
        if args.pretty:
            print(json.dumps(row, ensure_ascii=False, indent=2))
        else:
            print(json.dumps(row, ensure_ascii=False))
        count += 1
        if count >= args.limit:
            break
    if count == 0:
        print("No matching messages found.", file=sys.stderr)
    return 0


def cmd_export_csv(args: argparse.Namespace) -> int:
    parser = McapParser(args.mcap)
    result = parser.export_csv(
        output_dir=args.output_dir,
        topics=args.topic,
        start_time_ns=args.start_ns,
        end_time_ns=args.end_ns,
    )
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


def main() -> int:
    args = build_parser().parse_args()

    try:
        if args.command == "info":
            return cmd_info(args)
        if args.command == "show":
            return cmd_show(args)
        if args.command == "export-csv":
            return cmd_export_csv(args)
        raise ValueError(f"Unknown command: {args.command}")
    except FileNotFoundError as exc:
        print(str(exc), file=sys.stderr)
        return 1
    except BrokenPipeError:
        return 0
    except KeyboardInterrupt:
        print("Interrupted.", file=sys.stderr)
        return 130
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
