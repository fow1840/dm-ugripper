#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import struct
import subprocess
import sys
from collections import defaultdict
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Dict, Iterable, List, Tuple

from mcap.reader import make_reader


IMAGE_SCHEMA = "sensor_msgs/msg/CompressedImage"
CAMERA_INFO_SCHEMA = "sensor_msgs/msg/CameraInfo"
IMU_SCHEMA = "sensor_msgs/msg/Imu"


def clean_ffmpeg_env() -> dict[str, str]:
    env = os.environ.copy()
    env.pop("LD_LIBRARY_PATH", None)
    env.pop("CMAKE_PREFIX_PATH", None)
    return env


def align(offset: int, size: int) -> int:
    return offset + ((-(offset - 4)) % size)


def read_u32(buf: bytes, offset: int) -> Tuple[int, int]:
    offset = align(offset, 4)
    return struct.unpack_from("<I", buf, offset)[0], offset + 4


def read_i32(buf: bytes, offset: int) -> Tuple[int, int]:
    offset = align(offset, 4)
    return struct.unpack_from("<i", buf, offset)[0], offset + 4


def read_f64(buf: bytes, offset: int) -> Tuple[float, int]:
    offset = align(offset, 8)
    return struct.unpack_from("<d", buf, offset)[0], offset + 8


def read_bool(buf: bytes, offset: int) -> Tuple[bool, int]:
    return bool(buf[offset]), offset + 1


def read_string(buf: bytes, offset: int) -> Tuple[str, int]:
    size, offset = read_u32(buf, offset)
    raw = buf[offset : offset + size]
    offset += size
    if raw.endswith(b"\x00"):
        raw = raw[:-1]
    return raw.decode("utf-8", errors="replace"), offset


def read_bytes(buf: bytes, offset: int) -> Tuple[bytes, int]:
    size, offset = read_u32(buf, offset)
    return buf[offset : offset + size], offset + size


def parse_compressed_image(data: bytes) -> dict:
    offset = 4
    sec, offset = read_i32(data, offset)
    nsec, offset = read_u32(data, offset)
    frame_id, offset = read_string(data, offset)
    fmt, offset = read_string(data, offset)
    payload, offset = read_bytes(data, offset)
    return {
        "stamp_ns": sec * 1_000_000_000 + nsec,
        "frame_id": frame_id,
        "format": fmt,
        "payload": payload,
    }


def parse_camera_info(data: bytes) -> dict:
    offset = 4
    sec, offset = read_i32(data, offset)
    nsec, offset = read_u32(data, offset)
    frame_id, offset = read_string(data, offset)
    height, offset = read_u32(data, offset)
    width, offset = read_u32(data, offset)
    distortion_model, offset = read_string(data, offset)
    d_len, offset = read_u32(data, offset)
    d = []
    for _ in range(d_len):
        value, offset = read_f64(data, offset)
        d.append(value)
    k = []
    for _ in range(9):
        value, offset = read_f64(data, offset)
        k.append(value)
    r = []
    for _ in range(9):
        value, offset = read_f64(data, offset)
        r.append(value)
    p = []
    for _ in range(12):
        value, offset = read_f64(data, offset)
        p.append(value)
    binning_x, offset = read_u32(data, offset)
    binning_y, offset = read_u32(data, offset)
    roi_x_offset, offset = read_u32(data, offset)
    roi_y_offset, offset = read_u32(data, offset)
    roi_height, offset = read_u32(data, offset)
    roi_width, offset = read_u32(data, offset)
    do_rectify, offset = read_bool(data, offset)
    return {
        "stamp_ns": sec * 1_000_000_000 + nsec,
        "frame_id": frame_id,
        "height": height,
        "width": width,
        "distortion_model": distortion_model,
        "d": d,
        "k": k,
        "r": r,
        "p": p,
        "binning_x": binning_x,
        "binning_y": binning_y,
        "roi": {
            "x_offset": roi_x_offset,
            "y_offset": roi_y_offset,
            "height": roi_height,
            "width": roi_width,
            "do_rectify": do_rectify,
        },
    }


def parse_imu(data: bytes) -> dict:
    offset = 4
    sec, offset = read_i32(data, offset)
    nsec, offset = read_u32(data, offset)
    frame_id, offset = read_string(data, offset)
    orientation = []
    for _ in range(4):
        value, offset = read_f64(data, offset)
        orientation.append(value)
    orientation_covariance = []
    for _ in range(9):
        value, offset = read_f64(data, offset)
        orientation_covariance.append(value)
    angular_velocity = []
    for _ in range(3):
        value, offset = read_f64(data, offset)
        angular_velocity.append(value)
    angular_velocity_covariance = []
    for _ in range(9):
        value, offset = read_f64(data, offset)
        angular_velocity_covariance.append(value)
    linear_acceleration = []
    for _ in range(3):
        value, offset = read_f64(data, offset)
        linear_acceleration.append(value)
    linear_acceleration_covariance = []
    for _ in range(9):
        value, offset = read_f64(data, offset)
        linear_acceleration_covariance.append(value)
    return {
        "stamp_ns": sec * 1_000_000_000 + nsec,
        "frame_id": frame_id,
        "orientation": orientation,
        "orientation_covariance": orientation_covariance,
        "angular_velocity": angular_velocity,
        "angular_velocity_covariance": angular_velocity_covariance,
        "linear_acceleration": linear_acceleration,
        "linear_acceleration_covariance": linear_acceleration_covariance,
    }


def parse_encoder(data: bytes) -> dict:
    if len(data) < 8:
        raise ValueError(f"encoder payload too short: {len(data)}")
    raw, rad = struct.unpack_from("<if", data, 0)
    return {
        "raw": raw,
        "rad": rad,
    }


def slug_topic(topic: str) -> str:
    return topic.lstrip("/").replace("/", "_")


def normalize_video_format(value: str | None) -> str:
    normalized = (value or "").strip().lower()
    if normalized in {"h264", "avc", "avc1"}:
        return "h264"
    if normalized in {"h265", "hevc", "hev1", "hvc1"}:
        return "h265"
    return ""


def ffmpeg_demuxer_for_format(video_format: str) -> str:
    if video_format == "h265":
        return "hevc"
    return "h264"


def bitstream_suffix_for_format(video_format: str) -> str:
    return ".h265" if video_format == "h265" else ".h264"


def collect_mcap_files(path: Path) -> list[Path]:
    if path.is_file():
        return [path]
    if not path.exists():
        raise FileNotFoundError(path)

    metadata_path = path / "metadata.json"
    if metadata_path.is_file():
        try:
            metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
            require_files = metadata.get("require_files") or []
            candidates = [
                path / name
                for name in require_files
                if isinstance(name, str) and name.endswith(".mcap") and (path / name).is_file()
            ]
            if candidates:
                return sorted(candidates)
        except (OSError, json.JSONDecodeError):
            pass

    candidates = sorted(path.rglob("episode.mcap"))
    if candidates:
        return candidates

    candidates = []
    for metadata_path in sorted(path.rglob("metadata.json")):
        episode_dir = metadata_path.parent
        try:
            metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        for name in metadata.get("require_files") or []:
            if isinstance(name, str) and name.endswith(".mcap") and (episode_dir / name).is_file():
                candidates.append(episode_dir / name)
    if candidates:
        return sorted(candidates)

    candidates = sorted(path.glob("*.mcap"))
    if candidates:
        return candidates

    raise FileNotFoundError(f"no episode MCAP found under {path}")


@dataclass
class TopicStats:
    topic: str
    schema: str
    message_encoding: str
    count: int = 0
    first_log_time_ns: int | None = None
    last_log_time_ns: int | None = None
    first_stamp_ns: int | None = None
    last_stamp_ns: int | None = None
    payload_bytes: int = 0
    output_file: str = ""
    output_bytes: int = 0
    image_format: str = ""

    def update_log_time(self, log_time_ns: int) -> None:
        if self.first_log_time_ns is None or log_time_ns < self.first_log_time_ns:
            self.first_log_time_ns = log_time_ns
        if self.last_log_time_ns is None or log_time_ns > self.last_log_time_ns:
            self.last_log_time_ns = log_time_ns

    def update_stamp(self, stamp_ns: int) -> None:
        if self.first_stamp_ns is None or stamp_ns < self.first_stamp_ns:
            self.first_stamp_ns = stamp_ns
        if self.last_stamp_ns is None or stamp_ns > self.last_stamp_ns:
            self.last_stamp_ns = stamp_ns


def extract_episode(mcap_path: Path, out_root: Path, ffmpeg: str, keep_bitstream: bool) -> dict:
    with mcap_path.open("rb") as f:
        reader = make_reader(f)
        summary = reader.get_summary()
        schemas = {sid: s for sid, s in summary.schemas.items()}
        channels = list(summary.channels.values())

    topic_meta: dict[str, tuple[str, str]] = {}
    image_formats: dict[str, str] = {}
    image_topics: list[str] = []
    raw_topics: list[str] = []
    for channel in channels:
        schema = schemas.get(channel.schema_id)
        schema_name = schema.name if schema else ""
        topic_meta[channel.topic] = (schema_name, channel.message_encoding)
        if schema_name == IMAGE_SCHEMA and channel.message_encoding == "cdr":
            image_topics.append(channel.topic)
            image_formats[channel.topic] = normalize_video_format(
                (channel.metadata or {}).get("video_format")
            )
        else:
            raw_topics.append(channel.topic)

    image_topics.sort()
    raw_topics.sort()
    raw_topics = [topic for topic in raw_topics if topic not in image_topics]

    episode_name = mcap_path.parent.name
    episode_out = out_root / episode_name
    videos_dir = episode_out / "videos"
    raw_dir = episode_out / "raw"
    videos_dir.mkdir(parents=True, exist_ok=True)
    raw_dir.mkdir(parents=True, exist_ok=True)

    stats: dict[str, TopicStats] = {}
    image_tmp_paths: dict[str, Path] = {}
    image_handles: dict[str, object] = {}
    raw_handles: dict[str, object] = {}
    ffmpeg_env = clean_ffmpeg_env()

    for topic in image_topics:
        slug = slug_topic(topic)
        video_format = image_formats.get(topic) or "h264"
        image_tmp_paths[topic] = videos_dir / f"{slug}{bitstream_suffix_for_format(video_format)}"
        image_tmp_paths[topic].write_bytes(b"")
        image_handles[topic] = image_tmp_paths[topic].open("ab")
    for topic in raw_topics:
        schema_name, encoding = topic_meta[topic]
        slug = slug_topic(topic)
        if schema_name == CAMERA_INFO_SCHEMA or schema_name == IMU_SCHEMA:
            suffix = ".cdr"
        elif encoding == "json":
            suffix = ".jsonl"
        elif encoding == "binary":
            suffix = ".bin"
        else:
            suffix = ".raw"
        raw_handles[topic] = (raw_dir / f"{slug}{suffix}").open("wb")

    frame_intervals_us: dict[str, list[float]] = defaultdict(list)
    prev_log_time_ns: dict[str, int] = {}
    first_image_log_time_ns: int | None = None

    try:
        with mcap_path.open("rb") as f:
            reader = make_reader(f)
            for schema, channel, msg in reader.iter_messages():
                topic = channel.topic
                if topic not in topic_meta:
                    continue
                schema_name, encoding = topic_meta[topic]
                st = stats.setdefault(topic, TopicStats(topic=topic, schema=schema_name, message_encoding=encoding))
                st.count += 1
                st.update_log_time(msg.log_time)

                if topic in image_tmp_paths:
                    parsed = parse_compressed_image(msg.data)
                    video_format = normalize_video_format(parsed.get("format")) or image_formats.get(topic) or "h264"
                    image_formats[topic] = video_format
                    payload = parsed["payload"]
                    stamp_ns = parsed["stamp_ns"]
                    st.update_stamp(stamp_ns)
                    st.payload_bytes += len(payload)
                    st.image_format = video_format
                    image_handles[topic].write(payload)
                    if first_image_log_time_ns is None or msg.log_time < first_image_log_time_ns:
                        first_image_log_time_ns = msg.log_time
                else:
                    handle = raw_handles[topic]
                    handle.write(msg.data)
                    if encoding == "json":
                        handle.write(b"\n")
                    st.payload_bytes += len(msg.data)
                    if schema_name == CAMERA_INFO_SCHEMA:
                        parsed = parse_camera_info(msg.data)
                        st.update_stamp(parsed["stamp_ns"])
                    elif schema_name == IMU_SCHEMA:
                        parsed = parse_imu(msg.data)
                        st.update_stamp(parsed["stamp_ns"])
                    elif schema_name == "EncoderFrame":
                        parsed = parse_encoder(msg.data)
                        st.update_stamp(msg.log_time)

                if topic in prev_log_time_ns:
                    frame_intervals_us[topic].append((msg.log_time - prev_log_time_ns[topic]) / 1000.0)
                prev_log_time_ns[topic] = msg.log_time
    finally:
        for handle in image_handles.values():
            handle.close()
        for handle in raw_handles.values():
            handle.close()

    video_fps = {
        "cam_left": 60,
        "cam_right": 60,
        "tcam_left_l": 120,
        "tcam_left_r": 120,
        "tcam_right_l": 120,
        "tcam_right_r": 120,
        "stereo_left": 25,
        "stereo_right": 25,
    }
    for topic in image_topics:
        slug = slug_topic(topic).split("_image_compressed", 1)[0]
        src = image_tmp_paths[topic]
        dst = videos_dir / f"{slug}.mkv"
        video_format = image_formats.get(topic) or "h264"
        subprocess.run(
            [
                ffmpeg,
                "-hide_banner",
                "-loglevel",
                "error",
                "-y",
                "-f",
                ffmpeg_demuxer_for_format(video_format),
                "-r",
                str(video_fps.get(slug, 30)),
                "-i",
                str(src),
                "-c",
                "copy",
                str(dst),
            ],
            check=True,
            env=ffmpeg_env,
        )
        if not keep_bitstream:
            src.unlink(missing_ok=True)

        st = stats[topic]
        st.output_file = str(dst.relative_to(episode_out))
        st.output_bytes = dst.stat().st_size

    for topic in raw_topics:
        st = stats[topic]
        slug = slug_topic(topic)
        schema_name, encoding = topic_meta[topic]
        suffix = ".cdr" if schema_name in {CAMERA_INFO_SCHEMA, IMU_SCHEMA} else (".jsonl" if encoding == "json" else ".bin" if encoding == "binary" else ".raw")
        out_file = raw_dir / f"{slug}{suffix}"
        st.output_file = str(out_file.relative_to(episode_out))
        st.output_bytes = out_file.stat().st_size

    image_first_times = [st.first_log_time_ns for topic, st in stats.items() if topic in image_topics and st.first_log_time_ns is not None]
    image_sync = {}
    if image_first_times:
        base = min(image_first_times)
        image_sync = {
            "base_first_log_time_ns": base,
            "first_frame_spread_us": int((max(image_first_times) - base) / 1000),
            "topics": {
                topic: {
                    "first_offset_us": int((st.first_log_time_ns - base) / 1000) if st.first_log_time_ns is not None else None,
                    "frames": st.count,
                    "avg_interval_us": (sum(frame_intervals_us[topic]) / len(frame_intervals_us[topic])) if frame_intervals_us.get(topic) else None,
                }
                for topic, st in sorted(stats.items())
                if topic in image_topics
            },
        }

    manifest = {
        "source": str(mcap_path),
        "episode": episode_name,
        "source_bytes": mcap_path.stat().st_size,
        "output_root": str(episode_out),
        "topics": {
            topic: asdict(stats[topic])
            for topic in sorted(stats)
        },
        "image_sync": image_sync,
    }

    report_path = episode_out / "report.json"
    report_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Extract UGripper episode MCAPs into video and raw topic files.")
    parser.add_argument("path", type=Path, help="episode MCAP file or a directory containing episode MCAPs")
    parser.add_argument("--out-root", type=Path, help="output root directory (default: sibling extracted_mcap)")
    parser.add_argument("--ffmpeg", default="/usr/bin/ffmpeg", help="ffmpeg executable path")
    parser.add_argument("--keep-bitstream", action="store_true", help="keep intermediate .h264/.h265 files")
    parser.add_argument("--keep-h264", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--episode", action="append", help="only extract matching episode directory name; may be repeated")
    parser.add_argument("--json", action="store_true", help="print JSON summary only")
    args = parser.parse_args(argv)

    mcap_files = collect_mcap_files(args.path)
    if args.episode:
        wanted = set(args.episode)
        mcap_files = [path for path in mcap_files if path.parent.name in wanted]
    if not mcap_files:
        raise SystemExit("no MCAP files selected")

    out_root = args.out_root
    if out_root is None:
        if args.path.is_file():
            out_root = args.path.parent / "extracted_mcap"
        else:
            out_root = args.path / "extracted_mcap"
    out_root.mkdir(parents=True, exist_ok=True)

    summaries = []
    for mcap_path in mcap_files:
        manifest = extract_episode(mcap_path, out_root, args.ffmpeg, args.keep_bitstream or args.keep_h264)
        summaries.append(manifest)
        episode = manifest["episode"]
        image_sync = manifest.get("image_sync", {})
        print(f"{episode}: written to {manifest['output_root']}")
        if image_sync:
            print(f"  first_frame_spread_us={image_sync.get('first_frame_spread_us')}")
        for topic, info in manifest["topics"].items():
            print(f"  {topic}: count={info['count']} out={info['output_file']} bytes={info['output_bytes']}")

    if args.json:
        print(json.dumps({"episodes": summaries}, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
