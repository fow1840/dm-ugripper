#!/usr/bin/env python3
"""Verify that every decoded stereo frame has exactly one MCAP timestamp."""

import argparse
import json
import subprocess
from pathlib import Path

from mcap.stream_reader import StreamReader


def check_side(episode: Path, side: str) -> dict:
    video = episode / f"stereo_{side}.mkv"
    probe = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0", "-count_frames",
         "-show_entries", "stream=nb_read_frames", "-of", "json", str(video)],
        check=True, capture_output=True, text=True, timeout=180,
    )
    frames = int(json.loads(probe.stdout)["streams"][0]["nb_read_frames"])
    channels = {}
    samples = []
    with (episode / f"fays_data_{side}.mcap").open("rb") as handle:
        for record in StreamReader(handle).records:
            if type(record).__name__ == "Channel":
                channels[record.id] = record.topic
            elif type(record).__name__ == "Message" and channels.get(record.channel_id) == "c":
                if len(record.data) != 4:
                    raise ValueError("unexpected camera payload length")
                samples.append((int.from_bytes(record.data, "little"),
                                record.log_time, record.publish_time))
    errors = []
    if not frames or len(samples) != frames:
        errors.append(f"video={frames}, camera_timestamps={len(samples)}")
    if [s[0] for s in samples] != list(range(frames)):
        errors.append("frameIndex does not cover every video frame exactly once in write order")
    for column, name in [(1, "logTime"), (2, "publishTime")]:
        if any(b[column] <= a[column] for a, b in zip(samples, samples[1:])):
            errors.append(f"non-increasing {name}")
    return {"side": side, "video_frames": frames, "mcap_frames": len(samples),
            "last_frame_index": samples[-1][0] if samples else None,
            "decode_errors": probe.stderr, "errors": errors,
            "passed": not errors and not probe.stderr}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("episodes", nargs="+", type=Path)
    args = parser.parse_args()
    results = []
    for episode in args.episodes:
        for side in ("left", "right"):
            try:
                result = check_side(episode, side)
            except (OSError, ValueError, KeyError, IndexError, subprocess.SubprocessError) as exc:
                result = {"side": side, "passed": False, "errors": [str(exc)]}
            results.append({"episode": str(episode), **result})
    print(json.dumps(results, ensure_ascii=False, indent=2))
    return 0 if all(result["passed"] for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
