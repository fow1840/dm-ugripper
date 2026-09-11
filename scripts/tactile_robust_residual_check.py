#!/usr/bin/python3
"""Compatibility entrypoint for the standalone tactile damage detector."""

from pathlib import Path
import runpy


TOOL = (
    Path(__file__).resolve().parents[1]
    / "tools"
    / "tactile_damage_detector"
    / "tactile_damage_detector.py"
)
runpy.run_path(str(TOOL), run_name="__main__")
