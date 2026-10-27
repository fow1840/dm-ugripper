# Fays VIKit SDK Provenance

Source: https://cnb.cool/FaysTech/FaysSenseRelease/FaysSense_VI_Kit_Release

- Legacy UVC: lib/fays_atrak/<arch>/Release/libfays_vikit.so remains v3.8.4,
  commit 1bb5d83d111f25bf29dac5e081312829c1a941eb (unchanged bytes).
- FT601: lib/ft601/<arch>/libfays_vikit.so is test branch 3.9.7,
  commit 8277c6f853ecc581e239e2ce0b3aa6789dfb9948, synced 2026-10-26.
- Public headers use 3.9.7; the recorder uses the common 3.8.4 API.
- FTDI runtime remains 1.0.17 for both backends.

The launcher resolves USB descriptors at the configured physical side and starts
fays_record_example (UVC) or fays_record_ft601 (FT601) with isolated library paths.
FT601 stereo_dev_port is the sysfs directory name; IMU uses the same device.
