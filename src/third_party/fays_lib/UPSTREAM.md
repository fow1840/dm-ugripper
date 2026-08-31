# Fays VIKit SDK Provenance

- Source: https://cnb.cool/FaysTech/FaysSenseRelease/FaysSense_VI_Kit_Release
- Tag: `v3.8.3`
- Commit: `8e2d91a08b065bde247a2283ecaa4bb7fd3aa419`
- Release version: `3.8.3`
- SDK version: `3.8.3`
- Synced on: `2026-08-28`

The vendored subset contains the public VIKit headers, the aarch64 and x86_64
`libfays_vikit.so` runtime libraries, and the FT602 runtime libraries required
by `standalone/FaysStereoRecorder`.

For this update, `fays_vikit_version.h` and both architecture variants of
`libfays_vikit.so` changed. The upstream 3.8.3 release improves data stream
initialization stability while retaining the 3.8.2 IMU timestamp rollback fix.
The remaining vendored headers and FT602 `1.0.17` runtime libraries were verified
byte-for-byte unchanged from the 3.8.2 vendored subset.
