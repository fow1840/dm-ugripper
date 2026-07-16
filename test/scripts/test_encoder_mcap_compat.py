#!/usr/bin/env python3

import importlib.machinery
import importlib.util
import struct
import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


def load_module(name: str, path: Path):
    loader = importlib.machinery.SourceFileLoader(name, str(path))
    spec = importlib.util.spec_from_loader(name, loader)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    loader.exec_module(module)
    return module


reader = load_module("read_ugripper_mcap", REPO_ROOT / "py_script/read_ugripper_mcap.txt")
extractor = load_module("extract_mcap_episode", REPO_ROOT / "scripts/extract_mcap_episode.py")


class EncoderMcapCompatibilityTest(unittest.TestCase):
    def test_legacy_binary(self):
        payload = struct.pack("<if", -17, 0.625)

        decoded = reader.decode_encoder_payload(payload, "binary", "EncoderFrame")
        extracted = extractor.parse_encoder(payload)

        self.assertEqual(decoded["raw"], -17)
        self.assertAlmostEqual(decoded["rad"], 0.625)
        self.assertEqual(extracted["raw"], -17)
        self.assertAlmostEqual(extracted["rad"], 0.625)
        self.assertNotIn("stamp_ns", decoded)

    def test_ros2_cdr(self):
        stamp_ns = 1_700_000_000_123_456_789
        payload = b"\x00\x01\x00\x00" + struct.pack(
            "<iIif", 1_700_000_000, 123_456_789, 42, -1.5
        )

        decoded = reader.decode_encoder_payload(payload, "cdr", reader.ENCODER_SCHEMA)
        extracted = extractor.parse_encoder(payload, cdr=True)

        self.assertEqual(len(payload), 20)
        self.assertEqual(decoded["stamp_ns"], stamp_ns)
        self.assertEqual(decoded["raw"], 42)
        self.assertAlmostEqual(decoded["rad"], -1.5)
        self.assertEqual(extracted, {"stamp_ns": stamp_ns, "raw": 42, "rad": -1.5})

    def test_unified_topic_with_leading_slash(self):
        payload = b"\x00\x01\x00\x00" + struct.pack("<iIif", 10, 20, 30, 0.25)

        kind, decoded = reader.decode_payload(
            "/encoder_left", payload, "cdr", reader.ENCODER_SCHEMA
        )

        self.assertEqual(kind, "ugripper.encoder_cdr")
        self.assertEqual(decoded["stamp_ns"], 10_000_000_020)
        self.assertEqual(decoded["raw"], 30)

    def test_ros2_string_cdr_json(self):
        text = '{"calibration_info":{"format_version":"3.0"}}'
        encoded = text.encode("utf-8") + b"\x00"
        payload = b"\x00\x01\x00\x00" + struct.pack("<I", len(encoded)) + encoded

        kind, decoded = reader.decode_payload(
            "/calibration", payload, "cdr", reader.STRING_SCHEMA
        )

        self.assertEqual(kind, "json")
        self.assertEqual(decoded["calibration_info"]["format_version"], "3.0")
        self.assertEqual(extractor.parse_string_cdr(payload), text)


if __name__ == "__main__":
    unittest.main()
