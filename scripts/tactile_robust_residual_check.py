#!/usr/bin/python3
"""Compare two tactile images with the runtime robust residual metric."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys
from typing import Tuple

if "LD_LIBRARY_PATH" in os.environ and not os.environ.get("UGRIPPER_TACTILE_KEEP_LD_LIBRARY_PATH"):
    clean_env = dict(os.environ)
    clean_env.pop("LD_LIBRARY_PATH", None)
    clean_env["UGRIPPER_TACTILE_KEEP_LD_LIBRARY_PATH"] = "1"
    os.execve(sys.executable, [sys.executable, *sys.argv], clean_env)

import numpy as np
from PIL import Image


FRAME_WIDTH = 160
FRAME_HEIGHT = 120
FRAME_BYTES = FRAME_WIDTH * FRAME_HEIGHT
RESIDUAL_FLOOR = 10.0
MAD_TO_SIGMA = 1.4826
MAD_MULTIPLIER = 6.0
MIN_NEIGHBOR_COUNT = 3
MIN_COMPONENT_PIXELS = 8
ARTIFACT_MAX_COMPONENT_PIXELS = 25
ARTIFACT_LOWFREQ_MEAN_THRESHOLD = 5.0
LOWFREQ_BLUR_RADIUS = 2
ALIGNMENT_SEARCH_RADIUS = 2
ALIGNMENT_BLUR_RADIUS = 2
EDGE_LOCAL_TOLERANCE_RADIUS = 1
EDGE_MAGNITUDE_THRESHOLD = 24.0
EDGE_LOWFREQ_THRESHOLD = 8.0


def _resampling_filter() -> int:
    return getattr(getattr(Image, "Resampling", Image), "BILINEAR")


def load_tactile_frame(path: Path, preprocessed: bool = False) -> np.ndarray:
    if path.suffix.lower() == ".gray":
        data = path.read_bytes()
        if len(data) != FRAME_BYTES:
            raise ValueError(f"{path} has {len(data)} bytes, expected {FRAME_BYTES}")
        return np.frombuffer(data, dtype=np.uint8).reshape(FRAME_HEIGHT, FRAME_WIDTH).astype(np.float32)

    image = Image.open(path)
    if image.mode == "L" and image.size == (FRAME_WIDTH, FRAME_HEIGHT):
        return np.asarray(image, dtype=np.float32)

    if preprocessed:
        image = image.convert("L").resize((FRAME_WIDTH, FRAME_HEIGHT), getattr(getattr(Image, "Resampling", Image), "NEAREST"))
        return np.asarray(image, dtype=np.float32)

    image = image.convert("RGB")
    width, height = image.size
    left = int(width * 0.18)
    image = image.crop((left, 0, width, height))
    image = image.resize((FRAME_WIDTH, FRAME_HEIGHT), _resampling_filter()).convert("L")
    return np.asarray(image, dtype=np.float32)


def box_blur(image: np.ndarray, radius: int = LOWFREQ_BLUR_RADIUS) -> np.ndarray:
    if radius <= 0:
        return image.copy()
    padded = np.pad(image, radius, mode="edge")
    blurred = np.zeros(image.shape, dtype=np.float32)
    size = radius * 2 + 1
    for y_offset in range(size):
        for x_offset in range(size):
            blurred += padded[y_offset : y_offset + image.shape[0], x_offset : x_offset + image.shape[1]]
    return blurred / float(size * size)


def shift_with_edge(image: np.ndarray, shift_y: int, shift_x: int) -> np.ndarray:
    source_y = np.clip(np.arange(image.shape[0]) - shift_y, 0, image.shape[0] - 1)
    source_x = np.clip(np.arange(image.shape[1]) - shift_x, 0, image.shape[1] - 1)
    return image[np.ix_(source_y, source_x)]


def best_integer_translation(baseline: np.ndarray, current: np.ndarray) -> Tuple[np.ndarray, int, int, float]:
    baseline_blur = box_blur(baseline, ALIGNMENT_BLUR_RADIUS)
    current_blur = box_blur(current, ALIGNMENT_BLUR_RADIUS)
    margin = ALIGNMENT_SEARCH_RADIUS + 4
    best_score = float("inf")
    best_shift_x = 0
    best_shift_y = 0
    best_distance = 0
    roi = np.s_[margin:-margin, margin:-margin]
    for shift_y in range(-ALIGNMENT_SEARCH_RADIUS, ALIGNMENT_SEARCH_RADIUS + 1):
        for shift_x in range(-ALIGNMENT_SEARCH_RADIUS, ALIGNMENT_SEARCH_RADIUS + 1):
            shifted = shift_with_edge(current_blur, shift_y, shift_x)
            score = float(np.mean(np.abs(baseline_blur[roi] - shifted[roi])))
            distance = abs(shift_x) + abs(shift_y)
            if score < best_score - 1e-9 or (abs(score - best_score) <= 1e-9 and distance < best_distance):
                best_score = score
                best_shift_x = shift_x
                best_shift_y = shift_y
                best_distance = distance
    return shift_with_edge(current, best_shift_y, best_shift_x), best_shift_x, best_shift_y, best_score


def sobel_edge_mask(image: np.ndarray) -> np.ndarray:
    padded = np.pad(image, 1, mode="edge")
    top_left = padded[:-2, :-2]
    top = padded[:-2, 1:-1]
    top_right = padded[:-2, 2:]
    left = padded[1:-1, :-2]
    right = padded[1:-1, 2:]
    bottom_left = padded[2:, :-2]
    bottom = padded[2:, 1:-1]
    bottom_right = padded[2:, 2:]
    gx = -top_left + top_right - 2.0 * left + 2.0 * right - bottom_left + bottom_right
    gy = -top_left - 2.0 * top - top_right + bottom_left + 2.0 * bottom + bottom_right
    return np.hypot(gx, gy) >= EDGE_MAGNITUDE_THRESHOLD


def dilate_mask(mask: np.ndarray, radius: int) -> np.ndarray:
    padded = np.pad(mask, radius, mode="edge")
    dilated = np.zeros(mask.shape, dtype=bool)
    size = radius * 2 + 1
    for y_offset in range(size):
        for x_offset in range(size):
            dilated |= padded[y_offset : y_offset + mask.shape[0], x_offset : x_offset + mask.shape[1]]
    return dilated


def local_symmetric_residual(baseline: np.ndarray, current: np.ndarray, radius: int) -> np.ndarray:
    padded_current = np.pad(current, radius, mode="edge")
    padded_baseline = np.pad(baseline, radius, mode="edge")
    forward = np.full(baseline.shape, np.inf, dtype=np.float32)
    reverse = np.full(baseline.shape, np.inf, dtype=np.float32)
    size = radius * 2 + 1
    for y_offset in range(size):
        for x_offset in range(size):
            current_window = padded_current[
                y_offset : y_offset + baseline.shape[0], x_offset : x_offset + baseline.shape[1]
            ]
            baseline_window = padded_baseline[
                y_offset : y_offset + baseline.shape[0], x_offset : x_offset + baseline.shape[1]
            ]
            np.minimum(forward, np.abs(baseline - current_window), out=forward)
            np.minimum(reverse, np.abs(current - baseline_window), out=reverse)
    return np.maximum(forward, reverse)


def filter_residual_mask(mask: np.ndarray, lowfreq_diff: np.ndarray | None = None) -> Tuple[np.ndarray, int, int, int]:
    padded = np.pad(mask.astype(np.uint8), 1)
    neighbor_count = np.zeros(mask.shape, dtype=np.uint8)
    for y_offset in range(3):
        for x_offset in range(3):
            neighbor_count += padded[y_offset : y_offset + mask.shape[0], x_offset : x_offset + mask.shape[1]]
    neighbor_mask = mask & (neighbor_count >= MIN_NEIGHBOR_COUNT)

    height, width = neighbor_mask.shape
    visited = np.zeros(neighbor_mask.shape, dtype=bool)
    filtered = np.zeros(neighbor_mask.shape, dtype=bool)
    max_component_pixels = 0
    dropped_artifact_pixels = 0
    dropped_artifact_components = 0
    for y in range(height):
        for x in range(width):
            if not neighbor_mask[y, x] or visited[y, x]:
                continue
            stack = [(y, x)]
            visited[y, x] = True
            pixels = []
            for current_y, current_x in stack:
                pixels.append((current_y, current_x))
                for next_y in range(max(0, current_y - 1), min(height, current_y + 2)):
                    for next_x in range(max(0, current_x - 1), min(width, current_x + 2)):
                        if neighbor_mask[next_y, next_x] and not visited[next_y, next_x]:
                            visited[next_y, next_x] = True
                            stack.append((next_y, next_x))
            max_component_pixels = max(max_component_pixels, len(pixels))
            if len(pixels) >= MIN_COMPONENT_PIXELS:
                if lowfreq_diff is not None:
                    lowfreq_mean = float(np.mean([lowfreq_diff[pixel_y, pixel_x] for pixel_y, pixel_x in pixels]))
                else:
                    lowfreq_mean = float("inf")
                if len(pixels) <= ARTIFACT_MAX_COMPONENT_PIXELS and lowfreq_mean < ARTIFACT_LOWFREQ_MEAN_THRESHOLD:
                    dropped_artifact_pixels += len(pixels)
                    dropped_artifact_components += 1
                else:
                    for pixel_y, pixel_x in pixels:
                        filtered[pixel_y, pixel_x] = True
    return filtered, max_component_pixels, dropped_artifact_pixels, dropped_artifact_components


def robust_residual_area(baseline: np.ndarray, current: np.ndarray) -> Tuple[float, dict]:
    if baseline.shape != current.shape:
        raise ValueError(f"shape mismatch: baseline={baseline.shape} current={current.shape}")

    aligned, alignment_shift_x, alignment_shift_y, alignment_score = best_integer_translation(baseline, current)
    base_p5, base_p50, base_p95 = np.percentile(baseline, [5, 50, 95])
    current_p5, current_p50, current_p95 = np.percentile(aligned, [5, 50, 95])
    current_span = max(float(current_p95 - current_p5), 1e-6)
    gain = float((base_p95 - base_p5) / current_span)
    offset = float(base_p50 - gain * current_p50)

    corrected = np.clip(aligned * gain + offset, 0.0, 255.0)
    direct_residual = np.abs(corrected - baseline)
    median = float(np.median(direct_residual))
    mad = float(np.median(np.abs(direct_residual - median)))
    residual_threshold = max(RESIDUAL_FLOOR, median + MAD_MULTIPLIER * MAD_TO_SIGMA * mad)
    lowfreq_diff = np.abs(
        box_blur(corrected, LOWFREQ_BLUR_RADIUS) - box_blur(baseline, LOWFREQ_BLUR_RADIUS)
    )
    local_residual = local_symmetric_residual(baseline, corrected, EDGE_LOCAL_TOLERANCE_RADIUS)
    near_baseline_edge = dilate_mask(sobel_edge_mask(baseline), EDGE_LOCAL_TOLERANCE_RADIUS)
    near_current_edge = dilate_mask(sobel_edge_mask(corrected), EDGE_LOCAL_TOLERANCE_RADIUS)
    shift_like = (
        near_baseline_edge
        & near_current_edge
        & (local_residual < residual_threshold)
        & (lowfreq_diff < EDGE_LOWFREQ_THRESHOLD)
    )
    raw_mask = (direct_residual >= residual_threshold) & ~shift_like
    filtered_mask, max_component_pixels, dropped_artifact_pixels, dropped_artifact_components = filter_residual_mask(
        raw_mask,
        lowfreq_diff,
    )
    area = float(np.mean(filtered_mask))
    detail = {
        "robust_residual_area": area,
        "raw_residual_area": float(np.mean(raw_mask)),
        "residual_threshold": residual_threshold,
        "residual_median": median,
        "residual_mad": mad,
        "residual_mean": float(np.mean(direct_residual)),
        "residual_p95": float(np.percentile(direct_residual, 95)),
        "residual_p99": float(np.percentile(direct_residual, 99)),
        "gain": gain,
        "offset": offset,
        "alignment_shift_x": alignment_shift_x,
        "alignment_shift_y": alignment_shift_y,
        "alignment_score": alignment_score,
        "edge_tolerance_suppressed_pixels": int(np.sum((direct_residual >= residual_threshold) & shift_like)),
        "residual_floor": RESIDUAL_FLOOR,
        "mad_multiplier": MAD_MULTIPLIER,
        "min_neighbor_count": MIN_NEIGHBOR_COUNT,
        "min_component_pixels": MIN_COMPONENT_PIXELS,
        "max_component_pixels": max_component_pixels,
        "artifact_max_component_pixels": ARTIFACT_MAX_COMPONENT_PIXELS,
        "artifact_lowfreq_mean_threshold": ARTIFACT_LOWFREQ_MEAN_THRESHOLD,
        "artifact_dropped_pixels": dropped_artifact_pixels,
        "artifact_dropped_components": dropped_artifact_components,
    }
    return area, detail


def save_overlay(path: Path, current: np.ndarray, baseline: np.ndarray, detail: dict) -> None:
    gain = float(detail["gain"])
    offset = float(detail["offset"])
    aligned = shift_with_edge(current, int(detail["alignment_shift_y"]), int(detail["alignment_shift_x"]))
    corrected = np.clip(aligned * gain + offset, 0.0, 255.0)
    residual = np.abs(corrected - baseline)
    lowfreq_diff = np.abs(
        box_blur(corrected, LOWFREQ_BLUR_RADIUS) - box_blur(baseline, LOWFREQ_BLUR_RADIUS)
    )
    local_residual = local_symmetric_residual(baseline, corrected, EDGE_LOCAL_TOLERANCE_RADIUS)
    shift_like = (
        dilate_mask(sobel_edge_mask(baseline), EDGE_LOCAL_TOLERANCE_RADIUS)
        & dilate_mask(sobel_edge_mask(corrected), EDGE_LOCAL_TOLERANCE_RADIUS)
        & (local_residual < float(detail["residual_threshold"]))
        & (lowfreq_diff < EDGE_LOWFREQ_THRESHOLD)
    )
    raw_mask = (residual >= float(detail["residual_threshold"])) & ~shift_like
    mask, _, _, _ = filter_residual_mask(raw_mask, lowfreq_diff)
    overlay = np.stack([corrected, corrected, corrected], axis=2).astype(np.uint8)
    overlay[mask] = (overlay[mask] * 0.4 + np.array([0, 255, 0]) * 0.6).astype(np.uint8)
    image = Image.fromarray(overlay)
    image = image.resize((FRAME_WIDTH * 4, FRAME_HEIGHT * 4), _resampling_filter())
    image.save(path)


def main() -> int:
    global RESIDUAL_FLOOR, MAD_MULTIPLIER, MIN_NEIGHBOR_COUNT, MIN_COMPONENT_PIXELS
    global ARTIFACT_MAX_COMPONENT_PIXELS, ARTIFACT_LOWFREQ_MEAN_THRESHOLD, LOWFREQ_BLUR_RADIUS
    global ALIGNMENT_SEARCH_RADIUS, EDGE_LOCAL_TOLERANCE_RADIUS
    global EDGE_MAGNITUDE_THRESHOLD, EDGE_LOWFREQ_THRESHOLD

    parser = argparse.ArgumentParser(description="Run tactile robust residual damage check.")
    parser.add_argument("--baseline", required=True, type=Path, help="Baseline image or 160x120 .gray file.")
    parser.add_argument("--current", required=True, type=Path, help="Current image or 160x120 .gray file.")
    parser.add_argument("--threshold", type=float, default=0.003, help="Damage threshold for robust_residual_area.")
    parser.add_argument("--residual-floor", type=float, default=RESIDUAL_FLOOR, help="Minimum per-pixel residual threshold.")
    parser.add_argument("--mad-multiplier", type=float, default=MAD_MULTIPLIER, help="MAD multiplier for adaptive residual threshold.")
    parser.add_argument("--min-neighbor-count", type=int, default=MIN_NEIGHBOR_COUNT, help="Minimum active pixels in a 3x3 neighborhood.")
    parser.add_argument("--min-component-pixels", type=int, default=MIN_COMPONENT_PIXELS, help="Minimum connected component size to keep in the residual mask.")
    parser.add_argument("--artifact-max-component-pixels", type=int, default=ARTIFACT_MAX_COMPONENT_PIXELS, help="Drop retained components at or below this size when their low-frequency change is small.")
    parser.add_argument("--artifact-lowfreq-mean-threshold", type=float, default=ARTIFACT_LOWFREQ_MEAN_THRESHOLD, help="Drop small components whose mean low-frequency difference is below this threshold.")
    parser.add_argument("--lowfreq-blur-radius", type=int, default=LOWFREQ_BLUR_RADIUS, help="Box blur radius for low-frequency difference used by artifact filtering.")
    parser.add_argument("--alignment-search-radius", type=int, default=ALIGNMENT_SEARCH_RADIUS, help="Integer global translation search radius in pixels.")
    parser.add_argument("--edge-local-tolerance-radius", type=int, default=EDGE_LOCAL_TOLERANCE_RADIUS, help="Local bidirectional tolerance radius near texture edges.")
    parser.add_argument("--edge-magnitude-threshold", type=float, default=EDGE_MAGNITUDE_THRESHOLD, help="Sobel magnitude threshold used to identify texture edges.")
    parser.add_argument("--edge-lowfreq-threshold", type=float, default=EDGE_LOWFREQ_THRESHOLD, help="Maximum low-frequency difference for edge-shift suppression.")
    parser.add_argument("--json", action="store_true", help="Print JSON only.")
    parser.add_argument("--overlay", type=Path, help="Optional output path for a green residual mask overlay PNG.")
    parser.add_argument("--preprocessed", action="store_true", help="Treat image inputs as already cropped tactile frames and only resize them to 160x120.")
    args = parser.parse_args()

    RESIDUAL_FLOOR = args.residual_floor
    MAD_MULTIPLIER = args.mad_multiplier
    MIN_NEIGHBOR_COUNT = args.min_neighbor_count
    MIN_COMPONENT_PIXELS = args.min_component_pixels
    ARTIFACT_MAX_COMPONENT_PIXELS = args.artifact_max_component_pixels
    ARTIFACT_LOWFREQ_MEAN_THRESHOLD = args.artifact_lowfreq_mean_threshold
    LOWFREQ_BLUR_RADIUS = args.lowfreq_blur_radius
    ALIGNMENT_SEARCH_RADIUS = args.alignment_search_radius
    EDGE_LOCAL_TOLERANCE_RADIUS = args.edge_local_tolerance_radius
    EDGE_MAGNITUDE_THRESHOLD = args.edge_magnitude_threshold
    EDGE_LOWFREQ_THRESHOLD = args.edge_lowfreq_threshold

    baseline = load_tactile_frame(args.baseline, args.preprocessed)
    current = load_tactile_frame(args.current, args.preprocessed)
    score, detail = robust_residual_area(baseline, current)
    damaged = score >= args.threshold
    result = {
        "damaged": damaged,
        "threshold": args.threshold,
        **detail,
    }

    if args.overlay is not None:
        save_overlay(args.overlay, current, baseline, detail)
        result["overlay"] = str(args.overlay)

    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(f"damaged={str(damaged).lower()}")
        print(f"robust_residual_area={score:.6f}")
        print(f"raw_residual_area={detail['raw_residual_area']:.6f}")
        print(f"threshold={args.threshold:.6f}")
        print(f"residual_threshold={detail['residual_threshold']:.3f}")
        print(f"residual_floor={detail['residual_floor']:.3f}")
        print(f"residual_median={detail['residual_median']:.3f}")
        print(f"residual_mad={detail['residual_mad']:.3f}")
        print(f"mad_multiplier={detail['mad_multiplier']:.3f}")
        print(f"residual_p99={detail['residual_p99']:.3f}")
        print(f"alignment_shift_x={detail['alignment_shift_x']}")
        print(f"alignment_shift_y={detail['alignment_shift_y']}")
        print(f"alignment_score={detail['alignment_score']:.3f}")
        print(f"edge_tolerance_suppressed_pixels={detail['edge_tolerance_suppressed_pixels']}")
        print(f"max_component_pixels={detail['max_component_pixels']}")
        print(f"min_component_pixels={detail['min_component_pixels']}")
        print(f"artifact_dropped_pixels={detail['artifact_dropped_pixels']}")
        print(f"artifact_dropped_components={detail['artifact_dropped_components']}")
        print(f"artifact_max_component_pixels={detail['artifact_max_component_pixels']}")
        print(f"artifact_lowfreq_mean_threshold={detail['artifact_lowfreq_mean_threshold']:.3f}")
        print(f"gain={detail['gain']:.6f}")
        print(f"offset={detail['offset']:.3f}")
        if args.overlay is not None:
            print(f"overlay={args.overlay}")
    return 0 if not damaged else 2


if __name__ == "__main__":
    raise SystemExit(main())
