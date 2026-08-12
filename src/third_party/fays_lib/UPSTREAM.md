# Fays VIKit SDK Provenance

- Source: https://cnb.cool/FaysTech/FaysSenseRelease/FaysSense_VI_Kit_Release
- Tag: `v3.8.1`
- Commit: `d86cf9622b1c708a1e6efa9bf128e3e85838ccc7`
- Release version: `3.8.1`
- SDK version: `3.8.1`
- Synced on: `2026-08-12`

The vendored subset contains the public VIKit headers, the aarch64 and x86_64
`libfays_vikit.so` runtime libraries, and the FT602 runtime libraries required
by `standalone/FaysStereoRecorder`.

For this update, `fays_vikit_version.h` and both architecture variants of
`libfays_vikit.so` changed. The remaining vendored headers and FT602 `1.0.17`
runtime libraries were verified byte-for-byte unchanged.
