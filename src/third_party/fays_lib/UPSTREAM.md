# Fays VIKit SDK Provenance

- Source: https://cnb.cool/FaysTech/FaysSenseRelease/FaysSense_VI_Kit_Release
- Tag: `v3.8.4`
- Commit: `1bb5d83d111f25bf29dac5e081312829c1a941eb`
- Release version: `3.8.4`
- SDK version: `3.8.4`
- Synced on: `2026-10-19`

The vendored public headers and aarch64/x86_64 `libfays_vikit.so` match the
upstream tag. The FT602 runtime libraries remain at `1.0.17`.

Upstream 3.8.4 adds configurable V4L2 buffer counts. Both project SDK configs
set `imu_v4l2_buffer_count: 64` to reduce IMU receive-buffer overflows under
high load or scheduling delays; stereo image buffer settings are unchanged.
