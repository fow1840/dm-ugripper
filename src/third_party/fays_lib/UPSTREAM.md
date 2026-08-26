# Fays VIKit SDK Provenance

- Source: https://cnb.cool/FaysTech/FaysSenseRelease/FaysSense_VI_Kit_Release
- Branch: `feat-v3.8.2`
- Commit: `44573844547c5dfbe93e86445b458a211388282e`
- Release version: `3.8.2`
- SDK version: `3.8.2`
- Synced on: `2026-08-26`

The vendored subset contains the public VIKit headers, the aarch64 and x86_64
`libfays_vikit.so` runtime libraries, and the FT602 runtime libraries required
by `standalone/FaysStereoRecorder`.

For this update, `fays_vikit_version.h` and both architecture variants of
`libfays_vikit.so` changed. The upstream 3.8.2 release fixes a low-probability
IMU frame timestamp rollback that could cause frame drops. The remaining
vendored headers and FT602 `1.0.17` runtime libraries were verified byte-for-byte
unchanged from the 3.8.1 vendored subset.
