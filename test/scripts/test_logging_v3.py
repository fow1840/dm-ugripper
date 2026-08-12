#!/usr/bin/env python3

import importlib.util
import re
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


writer = load_module("ugripper_log_writer", ROOT / "scripts/logging/ugripper_log_writer.py")
logger = load_module("ugripper_logger", ROOT / "scripts/logging/ugripper_logger.py")
checker = load_module("check_main_log_v3", ROOT / "test/scripts/check_main_log_v3.py")


class LoggingV3Test(unittest.TestCase):
    def payload(self, line: str) -> str:
        return writer.V3_RE.fullmatch(line).group("payload")

    def test_phase0_golden_payloads(self):
        cases = {
            "[1784604654.707685][tid 2367][I][void RecordRuntime.setLedState 8242] [HMI_DIAG] category=led_target":
                "[HMI_DIAG] category=led_target",
            "[FAYS_TS 11:30:35.475191] [fays_stereo_daemon] right recorder maintain start ok":
                "[fays_stereo_daemon] right recorder maintain start ok",
            "[audio][INFO][2026-07-21 11:31:44] playing: ready":
                "[audio] playing: ready",
            "[FaysConfig] imu_dev_port: /dev/right_fays_imu -> /dev/video2":
                "[FaysConfig] imu_dev_port: /dev/right_fays_imu -> /dev/video2",
            "Camera 1 Intrinsics:": "Camera 1 Intrinsics:",
        }
        for raw, expected in cases.items():
            normalized = writer.normalize_line(raw)
            self.assertIsNotNone(normalized)
            self.assertEqual(self.payload(normalized), expected)

    def test_strips_ansi_and_maps_critical_to_fatal(self):
        raw = "[1784604654.707685][tid 2367]\x1b[91m[C]\x1b[0m[fn 1] fatal error_code=UGR-00-01"
        normalized = writer.normalize_line(raw)
        self.assertIn("[F][fn 1] fatal error_code=UGR-00-01", normalized)
        self.assertNotIn("\x1b", normalized)

    def test_suppresses_pygame_banner(self):
        self.assertIsNone(writer.normalize_line(writer.PYGAME_BANNER))

    def test_explicit_error_code_rejects_conflicting_payload_code(self):
        self.assertEqual(
            logger._append_error_code("failed error_code=UGR-22-06", "UGR-22-06"),
            "failed error_code=UGR-22-06",
        )
        with self.assertRaises(ValueError):
            logger._append_error_code("failed error_code=UGR-00-01", "UGR-22-06")
        with self.assertRaises(ValueError):
            logger._append_error_code("failed error_code=invalid", "UGR-22-06")

    def test_existing_same_day_log_is_normalized_before_append(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "main.log"
            path.write_text("[FaysConfig] imu_dev_port: /dev/a -> /dev/b\n", encoding="utf-8")
            writer.normalize_existing(path)
            line = path.read_text(encoding="utf-8").rstrip("\n")
            self.assertEqual(self.payload(line), "[FaysConfig] imu_dev_port: /dev/a -> /dev/b")

    def test_uncoded_legacy_error_log_is_archived_not_guessed(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "main.log"
            path.write_text(
                "[1784604654.707685][tid 2367][E][fn 1] old failure\n",
                encoding="utf-8",
            )
            writer.normalize_existing(path)
            self.assertFalse(path.exists())
            self.assertTrue(path.with_suffix(".log.pre-v3").is_file())

    def test_all_phase0_normalization_cases(self):
        fixture = ROOT / "test/data/logging/ugripper_logging_normalization_cases_v3.tsv"
        rows = fixture.read_text(encoding="utf-8").splitlines()[1:]
        for row in rows:
            case_id, _source, level, raw, expected, code, action = row.split("\t")
            raw_lines = raw.replace("\\n", "\n").splitlines()
            expected_lines = expected.replace("\\n", "\n").splitlines()
            if code != "-":
                raw_lines = [
                    line if "error_code=UGR-" in line else f"{line} error_code={code}"
                    for line in raw_lines
                ]
            normalized = [writer.normalize_line(line) for line in raw_lines]
            normalized = [line for line in normalized if line is not None]
            if action == "suppress_noise":
                self.assertEqual(normalized, [], case_id)
                continue
            self.assertEqual([self.payload(line) for line in normalized], expected_lines, case_id)
            self.assertTrue(all(f"][{level}][" in line for line in normalized), case_id)
            if code != "-":
                self.assertTrue(all(line.endswith(f"error_code={code}") for line in normalized), case_id)

    def test_main_log_checker_rejects_nested_v3_outer(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "main.log"
            path.write_text(
                "[1785728006.298998][tid 2197592][I][main_log_forwarder 0] "
                "Failed to connect: "
                "[1785728006.298905][tid 2197597][W]"
                "[void GripperHmiDriver.logConnectFailureLocked 292] "
                "GripperHmiTool0: cannot find port /dev/missing\n",
                encoding="utf-8",
            )
            self.assertEqual(
                checker.validate(path),
                ["line 1: nested V3 outer in payload"],
            )

    def test_main_log_checker_accepts_component_tag_in_payload(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "main.log"
            path.write_text(
                "[1785728006.298998][tid 2197592][I][main_log_forwarder 0] "
                "[HMI_DIAG] category=led_target\n",
                encoding="utf-8",
            )
            self.assertEqual(checker.validate(path), [])


if __name__ == "__main__":
    unittest.main()
