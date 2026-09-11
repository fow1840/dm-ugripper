#!/usr/bin/python3
"""Standalone tactile damage detector matching the UGripper runtime metric."""

from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import sys
import time
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
MIN_SOLID_CORE_PIXELS = 8
ALIGNMENT_SEARCH_RADIUS = 0
ALIGNMENT_BLUR_RADIUS = 2
EDGE_LOCAL_TOLERANCE_RADIUS = 0
EDGE_MAGNITUDE_THRESHOLD = 24.0
EDGE_LOWFREQ_THRESHOLD = 8.0
TONE_LUT_QUANTILES = (1, 5, 10, 25, 50, 75, 90, 95, 99)
TONE_LUT_MIN_MEAN_SHIFT = 8.0
TONE_LUT_MAX_RESIDUAL_P99 = 8.5
TONE_LUT_MAX_P99_RATIO = 0.60
BASELINE_MIN_CORRELATION = 0.90
BASELINE_BLOCK_COLUMNS = 10
BASELINE_BLOCK_ROWS = 16
BASELINE_BLOCK_MIN_CORRELATION = 0.85
BASELINE_MISMATCH_MAX_MATCHING_BLOCK_RATIO = 0.05
ALGORITHM_VERSION = "solid-core-tone-lut-v2"


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


def box_blur(image: np.ndarray, radius: int = 2) -> np.ndarray:
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
    if ALIGNMENT_SEARCH_RADIUS <= 0:
        return current.copy(), 0, 0, 0.0

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


def filter_residual_mask(mask: np.ndarray) -> Tuple[np.ndarray, int, int, int, int]:
    padded = np.pad(mask.astype(np.uint8), 1)
    neighbor_count = np.zeros(mask.shape, dtype=np.uint8)
    for y_offset in range(3):
        for x_offset in range(3):
            neighbor_count += padded[y_offset : y_offset + mask.shape[0], x_offset : x_offset + mask.shape[1]]
    neighbor_mask = mask & (neighbor_count >= MIN_NEIGHBOR_COUNT)

    padded_neighbor = np.pad(neighbor_mask.astype(np.uint8), 1)
    core_count = np.zeros(mask.shape, dtype=np.uint8)
    for y_offset in range(3):
        for x_offset in range(3):
            core_count += padded_neighbor[
                y_offset : y_offset + mask.shape[0], x_offset : x_offset + mask.shape[1]
            ]
    solid_core_mask = neighbor_mask & (core_count == 9)

    height, width = neighbor_mask.shape
    visited = np.zeros(neighbor_mask.shape, dtype=bool)
    filtered = np.zeros(neighbor_mask.shape, dtype=bool)
    max_component_pixels = 0
    max_solid_core_component_pixels = 0
    dropped_curve_pixels = 0
    dropped_curve_components = 0
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
            component_core_pixels = int(
                sum(bool(solid_core_mask[pixel_y, pixel_x]) for pixel_y, pixel_x in pixels)
            )
            max_solid_core_component_pixels = max(max_solid_core_component_pixels, component_core_pixels)
            if len(pixels) >= MIN_COMPONENT_PIXELS:
                if component_core_pixels < MIN_SOLID_CORE_PIXELS:
                    dropped_curve_pixels += len(pixels)
                    dropped_curve_components += 1
                else:
                    for pixel_y, pixel_x in pixels:
                        if solid_core_mask[pixel_y, pixel_x]:
                            filtered[pixel_y, pixel_x] = True
    return (
        filtered,
        max_component_pixels,
        max_solid_core_component_pixels,
        dropped_curve_pixels,
        dropped_curve_components,
    )


def quantile_tone_map(baseline: np.ndarray, current: np.ndarray) -> np.ndarray:
    source_points = np.percentile(current, TONE_LUT_QUANTILES)
    target_points = np.percentile(baseline, TONE_LUT_QUANTILES)
    unique_source, first_indices = np.unique(source_points, return_index=True)
    unique_target = target_points[first_indices]
    return np.interp(
        current,
        unique_source,
        unique_target,
        left=unique_target[0],
        right=unique_target[-1],
    ).astype(np.float32)


def pearson_correlation(first: np.ndarray, second: np.ndarray) -> float:
    first_centered = first - float(np.mean(first))
    second_centered = second - float(np.mean(second))
    denominator = float(np.sqrt(np.sum(first_centered * first_centered) * np.sum(second_centered * second_centered)))
    if denominator <= 1e-12:
        return 0.0
    return float(np.sum(first_centered * second_centered) / denominator)


def affine_residual_evaluation(baseline: np.ndarray, candidate: np.ndarray) -> dict:
    base_p5, base_p50, base_p95 = np.percentile(baseline, [5, 50, 95])
    current_p5, current_p50, current_p95 = np.percentile(candidate, [5, 50, 95])
    current_span = max(float(current_p95 - current_p5), 1e-6)
    gain = float((base_p95 - base_p5) / current_span)
    offset = float(base_p50 - gain * current_p50)
    corrected = np.clip(candidate * gain + offset, 0.0, 255.0)
    direct_residual = np.abs(corrected - baseline)
    median = float(np.median(direct_residual))
    mad = float(np.median(np.abs(direct_residual - median)))
    return {
        "candidate": candidate,
        "corrected": corrected,
        "correlation": pearson_correlation(baseline, candidate),
        "direct_residual": direct_residual,
        "gain": gain,
        "offset": offset,
        "residual_mad": mad,
        "residual_mean": float(np.mean(direct_residual)),
        "residual_median": median,
        "residual_p95": float(np.percentile(direct_residual, 95)),
        "residual_p99": float(np.percentile(direct_residual, 99)),
        "residual_threshold": max(RESIDUAL_FLOOR, median + MAD_MULTIPLIER * MAD_TO_SIGMA * mad),
    }


def select_brightness_correction(baseline: np.ndarray, aligned: np.ndarray) -> Tuple[dict, dict]:
    affine = affine_residual_evaluation(baseline, aligned)
    mean_shift = abs(float(np.mean(aligned) - np.mean(baseline)))
    if mean_shift < TONE_LUT_MIN_MEAN_SHIFT:
        return affine, {
            "affine_residual_p99": float(affine["residual_p99"]),
            "brightness_mean_shift": mean_shift,
            "tone_lut_applied": False,
            "tone_lut_attempted": False,
            "tone_lut_correlation_gain": 0.0,
            "tone_lut_residual_p99": float(affine["residual_p99"]),
        }

    tone_mapped = quantile_tone_map(baseline, aligned)
    tone = affine_residual_evaluation(baseline, tone_mapped)
    correlation_gain = float(tone["correlation"] - affine["correlation"])
    tone_lut_applied = (
        tone["residual_p99"] <= TONE_LUT_MAX_RESIDUAL_P99
        and tone["residual_p99"] <= affine["residual_p99"] * TONE_LUT_MAX_P99_RATIO
        and correlation_gain > 0.0
    )
    diagnostics = {
        "affine_residual_p99": float(affine["residual_p99"]),
        "brightness_mean_shift": mean_shift,
        "tone_lut_applied": tone_lut_applied,
        "tone_lut_attempted": True,
        "tone_lut_correlation_gain": correlation_gain,
        "tone_lut_residual_p99": float(tone["residual_p99"]),
    }
    return (tone if tone_lut_applied else affine), diagnostics


def matching_block_mask(baseline: np.ndarray, candidate: np.ndarray) -> Tuple[float, np.ndarray]:
    matching_blocks = 0
    total_blocks = BASELINE_BLOCK_COLUMNS * BASELINE_BLOCK_ROWS
    mismatch_mask = np.zeros(baseline.shape, dtype=bool)
    for block_y in range(BASELINE_BLOCK_ROWS):
        y_begin = block_y * baseline.shape[0] // BASELINE_BLOCK_ROWS
        y_end = (block_y + 1) * baseline.shape[0] // BASELINE_BLOCK_ROWS
        for block_x in range(BASELINE_BLOCK_COLUMNS):
            x_begin = block_x * baseline.shape[1] // BASELINE_BLOCK_COLUMNS
            x_end = (block_x + 1) * baseline.shape[1] // BASELINE_BLOCK_COLUMNS
            block_correlation = pearson_correlation(
                baseline[y_begin:y_end, x_begin:x_end],
                candidate[y_begin:y_end, x_begin:x_end],
            )
            if block_correlation >= BASELINE_BLOCK_MIN_CORRELATION:
                matching_blocks += 1
            else:
                mismatch_mask[y_begin:y_end, x_begin:x_end] = True
    return matching_blocks / total_blocks, mismatch_mask


def robust_residual_area(baseline: np.ndarray, current: np.ndarray) -> Tuple[float, dict]:
    if baseline.shape != current.shape:
        raise ValueError(f"shape mismatch: baseline={baseline.shape} current={current.shape}")

    aligned, alignment_shift_x, alignment_shift_y, alignment_score = best_integer_translation(baseline, current)
    evaluation, brightness_diagnostics = select_brightness_correction(baseline, aligned)
    corrected = evaluation["corrected"]
    direct_residual = evaluation["direct_residual"]
    residual_threshold = float(evaluation["residual_threshold"])
    baseline_correlation = float(evaluation["correlation"])
    baseline_matching_block_ratio, structural_mismatch_mask = matching_block_mask(
        baseline, evaluation["candidate"]
    )
    low_global_correlation = baseline_correlation < BASELINE_MIN_CORRELATION
    baseline_compatible = (
        not low_global_correlation
        or baseline_matching_block_ratio > BASELINE_MISMATCH_MAX_MATCHING_BLOCK_RATIO
    )
    if not baseline_compatible:
        detail = {
            "robust_residual_area": 0.0,
            "raw_residual_area": 0.0,
            "residual_threshold": residual_threshold,
            "residual_median": float(evaluation["residual_median"]),
            "residual_mad": float(evaluation["residual_mad"]),
            "residual_mean": float(evaluation["residual_mean"]),
            "residual_p95": float(evaluation["residual_p95"]),
            "residual_p99": float(evaluation["residual_p99"]),
            "gain": float(evaluation["gain"]),
            "offset": float(evaluation["offset"]),
            "alignment_shift_x": alignment_shift_x,
            "alignment_shift_y": alignment_shift_y,
            "alignment_score": alignment_score,
            "edge_tolerance_suppressed_pixels": 0,
            "residual_floor": RESIDUAL_FLOOR,
            "mad_multiplier": MAD_MULTIPLIER,
            "min_neighbor_count": MIN_NEIGHBOR_COUNT,
            "min_component_pixels": MIN_COMPONENT_PIXELS,
            "min_solid_core_pixels": MIN_SOLID_CORE_PIXELS,
            "solid_core_pixels": 0,
            "max_component_pixels": 0,
            "max_solid_core_component_pixels": 0,
            "curve_dropped_pixels": 0,
            "curve_dropped_components": 0,
            "baseline_compatible": False,
            "baseline_correlation": baseline_correlation,
            "baseline_matching_block_ratio": baseline_matching_block_ratio,
            "baseline_refresh_required": True,
            "damage_reason": "baseline_mismatch",
            "large_structural_damage": False,
            **brightness_diagnostics,
        }
        return 0.0, detail
    if low_global_correlation:
        structural_pixels = int(np.sum(structural_mismatch_mask))
        structural_area = structural_pixels / FRAME_BYTES
        detail = {
            "robust_residual_area": structural_area,
            "raw_residual_area": structural_area,
            "residual_threshold": residual_threshold,
            "residual_median": float(evaluation["residual_median"]),
            "residual_mad": float(evaluation["residual_mad"]),
            "residual_mean": float(evaluation["residual_mean"]),
            "residual_p95": float(evaluation["residual_p95"]),
            "residual_p99": float(evaluation["residual_p99"]),
            "gain": float(evaluation["gain"]),
            "offset": float(evaluation["offset"]),
            "alignment_shift_x": alignment_shift_x,
            "alignment_shift_y": alignment_shift_y,
            "alignment_score": alignment_score,
            "edge_tolerance_suppressed_pixels": 0,
            "residual_floor": RESIDUAL_FLOOR,
            "mad_multiplier": MAD_MULTIPLIER,
            "min_neighbor_count": MIN_NEIGHBOR_COUNT,
            "min_component_pixels": MIN_COMPONENT_PIXELS,
            "min_solid_core_pixels": MIN_SOLID_CORE_PIXELS,
            "solid_core_pixels": structural_pixels,
            "max_component_pixels": structural_pixels,
            "max_solid_core_component_pixels": structural_pixels,
            "curve_dropped_pixels": 0,
            "curve_dropped_components": 0,
            "baseline_compatible": True,
            "baseline_correlation": baseline_correlation,
            "baseline_matching_block_ratio": baseline_matching_block_ratio,
            "baseline_refresh_required": False,
            "damage_reason": "large_structural_damage",
            "large_structural_damage": True,
            **brightness_diagnostics,
        }
        return structural_area, detail
    shift_like = np.zeros(baseline.shape, dtype=bool)
    if EDGE_LOCAL_TOLERANCE_RADIUS > 0:
        lowfreq_diff = np.abs(box_blur(corrected, 2) - box_blur(baseline, 2))
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
    (
        filtered_mask,
        max_component_pixels,
        max_solid_core_component_pixels,
        dropped_curve_pixels,
        dropped_curve_components,
    ) = filter_residual_mask(raw_mask)
    area = float(np.mean(filtered_mask))
    detail = {
        "robust_residual_area": area,
        "raw_residual_area": float(np.mean(raw_mask)),
        "residual_threshold": residual_threshold,
        "residual_median": float(evaluation["residual_median"]),
        "residual_mad": float(evaluation["residual_mad"]),
        "residual_mean": float(evaluation["residual_mean"]),
        "residual_p95": float(evaluation["residual_p95"]),
        "residual_p99": float(evaluation["residual_p99"]),
        "gain": float(evaluation["gain"]),
        "offset": float(evaluation["offset"]),
        "alignment_shift_x": alignment_shift_x,
        "alignment_shift_y": alignment_shift_y,
        "alignment_score": alignment_score,
        "edge_tolerance_suppressed_pixels": int(np.sum((direct_residual >= residual_threshold) & shift_like)),
        "residual_floor": RESIDUAL_FLOOR,
        "mad_multiplier": MAD_MULTIPLIER,
        "min_neighbor_count": MIN_NEIGHBOR_COUNT,
        "min_component_pixels": MIN_COMPONENT_PIXELS,
        "min_solid_core_pixels": MIN_SOLID_CORE_PIXELS,
        "solid_core_pixels": int(np.sum(filtered_mask)),
        "max_component_pixels": max_component_pixels,
        "max_solid_core_component_pixels": max_solid_core_component_pixels,
        "curve_dropped_pixels": dropped_curve_pixels,
        "curve_dropped_components": dropped_curve_components,
        "baseline_compatible": True,
        "baseline_correlation": baseline_correlation,
        "baseline_matching_block_ratio": baseline_matching_block_ratio,
        "baseline_refresh_required": False,
        "damage_reason": "solid_core" if int(np.sum(filtered_mask)) >= MIN_SOLID_CORE_PIXELS else "clean",
        "large_structural_damage": False,
        **brightness_diagnostics,
    }
    return area, detail


def corrected_from_detail(current: np.ndarray, baseline: np.ndarray, detail: dict) -> np.ndarray:
    gain = float(detail["gain"])
    offset = float(detail["offset"])
    aligned = shift_with_edge(current, int(detail["alignment_shift_y"]), int(detail["alignment_shift_x"]))
    candidate = quantile_tone_map(baseline, aligned) if detail.get("tone_lut_applied") else aligned
    return np.clip(candidate * gain + offset, 0.0, 255.0)


def filtered_mask_from_detail(current: np.ndarray, baseline: np.ndarray, detail: dict) -> np.ndarray:
    if not detail.get("baseline_compatible", True):
        return np.zeros(baseline.shape, dtype=bool)
    if detail.get("large_structural_damage"):
        aligned = shift_with_edge(
            current,
            int(detail["alignment_shift_y"]),
            int(detail["alignment_shift_x"]),
        )
        candidate = quantile_tone_map(baseline, aligned) if detail.get("tone_lut_applied") else aligned
        _, structural_mismatch_mask = matching_block_mask(baseline, candidate)
        return structural_mismatch_mask
    corrected = corrected_from_detail(current, baseline, detail)
    residual = np.abs(corrected - baseline)
    shift_like = np.zeros(baseline.shape, dtype=bool)
    if EDGE_LOCAL_TOLERANCE_RADIUS > 0:
        lowfreq_diff = np.abs(box_blur(corrected, 2) - box_blur(baseline, 2))
        local_residual = local_symmetric_residual(baseline, corrected, EDGE_LOCAL_TOLERANCE_RADIUS)
        shift_like = (
            dilate_mask(sobel_edge_mask(baseline), EDGE_LOCAL_TOLERANCE_RADIUS)
            & dilate_mask(sobel_edge_mask(corrected), EDGE_LOCAL_TOLERANCE_RADIUS)
            & (local_residual < float(detail["residual_threshold"]))
            & (lowfreq_diff < EDGE_LOWFREQ_THRESHOLD)
        )
    raw_mask = (residual >= float(detail["residual_threshold"])) & ~shift_like
    mask, _, _, _, _ = filter_residual_mask(raw_mask)
    return mask


def save_overlay(path: Path, current: np.ndarray, baseline: np.ndarray, detail: dict) -> None:
    mask = filtered_mask_from_detail(current, baseline, detail)
    corrected = corrected_from_detail(current, baseline, detail)
    overlay = np.stack([corrected, corrected, corrected], axis=2).astype(np.uint8)
    overlay[mask] = (overlay[mask] * 0.4 + np.array([0, 255, 0]) * 0.6).astype(np.uint8)
    image = Image.fromarray(overlay)
    image = image.resize((FRAME_WIDTH * 4, FRAME_HEIGHT * 4), _resampling_filter())
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path)


def save_mask(path: Path, mask: np.ndarray) -> None:
    image = Image.fromarray(mask.astype(np.uint8) * 255, mode="L")
    image = image.resize((FRAME_WIDTH * 4, FRAME_HEIGHT * 4), getattr(getattr(Image, "Resampling", Image), "NEAREST"))
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path)


def save_comparison(
    path: Path,
    baseline: np.ndarray,
    current: np.ndarray,
    mask: np.ndarray,
    detail: dict | None = None,
) -> None:
    from PIL import ImageDraw

    scale = 4
    panel_width = FRAME_WIDTH * scale
    panel_height = FRAME_HEIGHT * scale
    label_height = 24

    baseline_image = Image.fromarray(np.clip(baseline, 0, 255).astype(np.uint8), mode="L").convert("RGB")
    current_image = Image.fromarray(np.clip(current, 0, 255).astype(np.uint8), mode="L").convert("RGB")
    overlay_array = np.stack([current, current, current], axis=2).clip(0, 255).astype(np.uint8)
    if detail is not None and not detail.get("baseline_compatible", True):
        overlay_array = (
            overlay_array * 0.55 + np.array([255, 190, 0], dtype=np.float32) * 0.45
        ).astype(np.uint8)
        overlay_label = "BASELINE MISMATCH"
    else:
        overlay_array[mask] = (
            overlay_array[mask] * 0.35 + np.array([255, 0, 0]) * 0.65
        ).astype(np.uint8)
        overlay_label = "DAMAGE MASK" if np.any(mask) else "CLEAN"
    overlay_image = Image.fromarray(overlay_array, mode="RGB")

    resample = getattr(getattr(Image, "Resampling", Image), "NEAREST")
    panels = [
        baseline_image.resize((panel_width, panel_height), resample),
        current_image.resize((panel_width, panel_height), resample),
        overlay_image.resize((panel_width, panel_height), resample),
    ]
    comparison = Image.new("RGB", (panel_width * len(panels), panel_height + label_height), "white")
    draw = ImageDraw.Draw(comparison)
    for index, (label, panel) in enumerate(zip(("BASELINE", "INPUT", overlay_label), panels)):
        x = index * panel_width
        comparison.paste(panel, (x, label_height))
        draw.text((x + 8, 6), label, fill="black")
    path.parent.mkdir(parents=True, exist_ok=True)
    comparison.save(path)


def main() -> int:
    global RESIDUAL_FLOOR, MAD_MULTIPLIER, MIN_NEIGHBOR_COUNT, MIN_COMPONENT_PIXELS
    global MIN_SOLID_CORE_PIXELS
    global ALIGNMENT_SEARCH_RADIUS, EDGE_LOCAL_TOLERANCE_RADIUS
    global EDGE_MAGNITUDE_THRESHOLD, EDGE_LOWFREQ_THRESHOLD

    parser = argparse.ArgumentParser(description="Run tactile robust residual damage check.")
    parser.add_argument("--baseline", required=True, type=Path, help="Baseline image or 160x120 .gray file.")
    parser.add_argument("--current", "--image", dest="current", required=True, type=Path, help="Current image or 160x120 .gray file.")
    parser.add_argument("--threshold", type=float, help="Deprecated compatibility option; converts an area ratio to a solid-core pixel threshold.")
    parser.add_argument("--residual-floor", type=float, default=RESIDUAL_FLOOR, help="Minimum per-pixel residual threshold.")
    parser.add_argument("--mad-multiplier", type=float, default=MAD_MULTIPLIER, help="MAD multiplier for adaptive residual threshold.")
    parser.add_argument("--min-neighbor-count", type=int, default=MIN_NEIGHBOR_COUNT, help="Minimum active pixels in a 3x3 neighborhood.")
    parser.add_argument("--min-component-pixels", type=int, default=MIN_COMPONENT_PIXELS, help="Minimum connected component size to keep in the residual mask.")
    parser.add_argument("--min-solid-core-pixels", type=int, default=MIN_SOLID_CORE_PIXELS, help="Minimum 3x3 solid-core pixels required in one component.")
    parser.add_argument("--alignment-search-radius", type=int, default=ALIGNMENT_SEARCH_RADIUS, help="Integer global translation search radius in pixels.")
    parser.add_argument("--edge-local-tolerance-radius", type=int, default=EDGE_LOCAL_TOLERANCE_RADIUS, help="Local bidirectional tolerance radius near texture edges.")
    parser.add_argument("--edge-magnitude-threshold", type=float, default=EDGE_MAGNITUDE_THRESHOLD, help="Sobel magnitude threshold used to identify texture edges.")
    parser.add_argument("--edge-lowfreq-threshold", type=float, default=EDGE_LOWFREQ_THRESHOLD, help="Maximum low-frequency difference for edge-shift suppression.")
    parser.add_argument("--json", action="store_true", help="Print JSON only.")
    parser.add_argument("--overlay", type=Path, help="Optional output path for a green residual mask overlay PNG.")
    parser.add_argument("--mask", type=Path, help="Optional output path for the final binary solid-core mask PNG.")
    parser.add_argument("--comparison", type=Path, help="Optional output path for a baseline/input/mask comparison PNG.")
    parser.add_argument(
        "--output-dir",
        type=Path,
        help="Write result.json, mask.png and comparison.png to this directory.",
    )
    parser.add_argument("--preprocessed", action="store_true", help="Treat image inputs as already cropped tactile frames and only resize them to 160x120.")
    args = parser.parse_args()

    RESIDUAL_FLOOR = args.residual_floor
    MAD_MULTIPLIER = args.mad_multiplier
    MIN_NEIGHBOR_COUNT = args.min_neighbor_count
    MIN_COMPONENT_PIXELS = args.min_component_pixels
    MIN_SOLID_CORE_PIXELS = args.min_solid_core_pixels
    if args.threshold is not None:
        MIN_SOLID_CORE_PIXELS = max(1, math.ceil(args.threshold * FRAME_BYTES))
    ALIGNMENT_SEARCH_RADIUS = args.alignment_search_radius
    EDGE_LOCAL_TOLERANCE_RADIUS = args.edge_local_tolerance_radius
    EDGE_MAGNITUDE_THRESHOLD = args.edge_magnitude_threshold
    EDGE_LOWFREQ_THRESHOLD = args.edge_lowfreq_threshold

    detection_started = time.perf_counter()
    try:
        baseline = load_tactile_frame(args.baseline, args.preprocessed)
        current = load_tactile_frame(args.current, args.preprocessed)
        score, detail = robust_residual_area(baseline, current)
    except Exception as exc:
        print(json.dumps({"error": str(exc), "damaged": None}, ensure_ascii=False), file=sys.stderr)
        return 1

    damaged = detail["solid_core_pixels"] >= MIN_SOLID_CORE_PIXELS
    result = {
        "algorithm_version": ALGORITHM_VERSION,
        "baseline": str(args.baseline),
        "damaged": damaged,
        "detection_time_ms": (time.perf_counter() - detection_started) * 1000.0,
        "image": str(args.current),
        "preprocessed": args.preprocessed,
        "threshold": MIN_SOLID_CORE_PIXELS / FRAME_BYTES,
        **detail,
    }

    mask = None
    if args.mask is not None or args.comparison is not None or args.output_dir is not None:
        mask = filtered_mask_from_detail(current, baseline, detail)

    if args.output_dir is not None:
        args.output_dir.mkdir(parents=True, exist_ok=True)
        output_mask = args.output_dir / "mask.png"
        output_comparison = args.output_dir / "comparison.png"
        save_mask(output_mask, mask)
        save_comparison(output_comparison, baseline, current, mask, detail)
        result["mask"] = str(output_mask)
        result["comparison"] = str(output_comparison)

    if args.overlay is not None:
        save_overlay(args.overlay, current, baseline, detail)
        result["overlay"] = str(args.overlay)

    if args.mask is not None:
        save_mask(args.mask, mask)
        result["mask"] = str(args.mask)

    if args.comparison is not None:
        save_comparison(args.comparison, baseline, current, mask, detail)
        result["comparison"] = str(args.comparison)

    if args.output_dir is not None:
        result_path = args.output_dir / "result.json"
        result["result"] = str(result_path)
        result_path.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(f"damaged={str(damaged).lower()}")
        print(f"algorithm_version={ALGORITHM_VERSION}")
        print(f"detection_time_ms={result['detection_time_ms']:.3f}")
        print(f"robust_residual_area={score:.6f}")
        print(f"raw_residual_area={detail['raw_residual_area']:.6f}")
        print(f"solid_core_pixels={detail['solid_core_pixels']}")
        print(f"min_solid_core_pixels={detail['min_solid_core_pixels']}")
        print(f"residual_threshold={detail['residual_threshold']:.3f}")
        print(f"residual_floor={detail['residual_floor']:.3f}")
        print(f"residual_median={detail['residual_median']:.3f}")
        print(f"residual_mad={detail['residual_mad']:.3f}")
        print(f"mad_multiplier={detail['mad_multiplier']:.3f}")
        print(f"residual_p99={detail['residual_p99']:.3f}")
        print(f"affine_residual_p99={detail['affine_residual_p99']:.3f}")
        print(f"tone_lut_residual_p99={detail['tone_lut_residual_p99']:.3f}")
        print(f"tone_lut_correlation_gain={detail['tone_lut_correlation_gain']:.6f}")
        print(f"tone_lut_attempted={str(detail['tone_lut_attempted']).lower()}")
        print(f"tone_lut_applied={str(detail['tone_lut_applied']).lower()}")
        print(f"baseline_correlation={detail['baseline_correlation']:.6f}")
        print(f"baseline_compatible={str(detail['baseline_compatible']).lower()}")
        print(f"baseline_matching_block_ratio={detail['baseline_matching_block_ratio']:.6f}")
        print(f"baseline_refresh_required={str(detail['baseline_refresh_required']).lower()}")
        print(f"damage_reason={detail['damage_reason']}")
        print(f"alignment_shift_x={detail['alignment_shift_x']}")
        print(f"alignment_shift_y={detail['alignment_shift_y']}")
        print(f"alignment_score={detail['alignment_score']:.3f}")
        print(f"edge_tolerance_suppressed_pixels={detail['edge_tolerance_suppressed_pixels']}")
        print(f"max_component_pixels={detail['max_component_pixels']}")
        print(f"max_solid_core_component_pixels={detail['max_solid_core_component_pixels']}")
        print(f"min_component_pixels={detail['min_component_pixels']}")
        print(f"curve_dropped_pixels={detail['curve_dropped_pixels']}")
        print(f"curve_dropped_components={detail['curve_dropped_components']}")
        print(f"gain={detail['gain']:.6f}")
        print(f"offset={detail['offset']:.3f}")
        if args.overlay is not None:
            print(f"overlay={args.overlay}")
        if args.output_dir is not None:
            print(f"result={args.output_dir / 'result.json'}")
            print(f"mask={args.output_dir / 'mask.png'}")
            print(f"comparison={args.output_dir / 'comparison.png'}")
    return 0 if not damaged else 2


if __name__ == "__main__":
    raise SystemExit(main())
