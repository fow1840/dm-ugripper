#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import numpy as np
from PIL import Image


REPO_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPO_ROOT / "tools" / "tactile_damage_detector" / "tactile_damage_detector.py"


def load_tool_module():
    spec = importlib.util.spec_from_file_location("tactile_detector", TOOL_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class TactileDamageDetectorTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tool = load_tool_module()

    @staticmethod
    def texture() -> np.ndarray:
        y, x = np.indices((120, 160))
        return (60 + (x * 3 + y * 5) % 121).astype(np.uint8)

    def test_identical_pair_is_clean(self):
        frame = self.texture().astype(np.float32)
        _, detail = self.tool.robust_residual_area(frame, frame)
        self.assertEqual(detail["solid_core_pixels"], 0)

    def test_two_pixel_line_is_filtered(self):
        baseline = self.texture()
        current = baseline.copy()
        current[8:112, 78:80] = np.clip(current[8:112, 78:80].astype(int) + 70, 0, 255)
        _, detail = self.tool.robust_residual_area(
            baseline.astype(np.float32), current.astype(np.float32)
        )
        self.assertEqual(detail["solid_core_pixels"], 0)
        self.assertGreater(detail["curve_dropped_pixels"], 0)

    def test_conditional_tone_lut_filters_nonlinear_brightness(self):
        baseline = self.texture().astype(np.float32)
        current = np.rint(np.power(baseline / 255.0, 0.78) * 255.0).astype(np.float32)
        _, detail = self.tool.robust_residual_area(baseline, current)
        self.assertTrue(detail["tone_lut_applied"])
        self.assertEqual(detail["solid_core_pixels"], 0)

    def test_damage_survives_nonlinear_brightness(self):
        baseline = self.texture().astype(np.float32)
        current = np.rint(np.power(baseline / 255.0, 0.78) * 255.0)
        current[45:61, 68:84] = np.clip(current[45:61, 68:84] + 70, 0, 255)
        _, detail = self.tool.robust_residual_area(baseline, current.astype(np.float32))
        self.assertGreaterEqual(detail["solid_core_pixels"], 8)

    def test_different_texture_is_baseline_mismatch(self):
        baseline = self.texture().astype(np.float32)
        y, x = np.indices((120, 160))
        current = (60 + (x * 11 + y * 17) % 121).astype(np.float32)
        _, detail = self.tool.robust_residual_area(baseline, current)
        self.assertFalse(detail["baseline_compatible"])
        self.assertTrue(detail["baseline_refresh_required"])
        self.assertEqual(detail["damage_reason"], "baseline_mismatch")
        self.assertEqual(detail["solid_core_pixels"], 0)
        self.assertFalse(detail["tone_lut_applied"])

    def test_ninety_percent_damage_is_not_baseline_mismatch(self):
        baseline = self.texture().astype(np.float32)
        current = baseline.copy()
        current[:108, :] = 255.0 - current[:108, :]
        _, detail = self.tool.robust_residual_area(baseline, current)
        self.assertTrue(detail["baseline_compatible"])
        self.assertFalse(detail["baseline_refresh_required"])
        self.assertTrue(detail["large_structural_damage"])
        self.assertEqual(detail["damage_reason"], "large_structural_damage")

    def test_solid_damage_and_output_contract(self):
        baseline = self.texture()
        current = baseline.copy()
        current[45:61, 68:84] = np.clip(current[45:61, 68:84].astype(int) + 70, 0, 255)

        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            baseline_path = root / "baseline.png"
            current_path = root / "current.png"
            output_dir = root / "result"
            Image.fromarray(baseline, mode="L").save(baseline_path)
            Image.fromarray(current, mode="L").save(current_path)

            completed = subprocess.run(
                [
                    sys.executable,
                    str(TOOL_PATH),
                    "--baseline",
                    str(baseline_path),
                    "--image",
                    str(current_path),
                    "--preprocessed",
                    "--output-dir",
                    str(output_dir),
                    "--json",
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(completed.returncode, 2, completed.stderr)
            result = json.loads((output_dir / "result.json").read_text(encoding="utf-8"))
            self.assertTrue(result["damaged"])
            self.assertEqual(result["algorithm_version"], "solid-core-tone-lut-v2")
            self.assertGreater(result["detection_time_ms"], 0.0)
            self.assertGreaterEqual(result["solid_core_pixels"], 8)
            self.assertTrue((output_dir / "mask.png").is_file())
            self.assertTrue((output_dir / "comparison.png").is_file())


if __name__ == "__main__":
    unittest.main()
