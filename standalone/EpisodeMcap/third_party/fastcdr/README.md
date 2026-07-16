# Fast-CDR 1.0.23

This directory contains the minimal source set needed to build eProsima
Fast-CDR 1.0.23 as a static UGripper dependency.

- Upstream: https://github.com/eProsima/Fast-CDR
- Source archive: Ubuntu Jammy `fastcdr_1.0.23.orig.tar.gz`
- Source SHA-256: `6f7c9c6c0c82c150b5ea2b0a58d5c9a466b87a1fcfca40d5786d99d4963a6721`
- License: Apache-2.0, see `LICENSE`

The local CMake target intentionally excludes upstream tests, packaging, and
shared-library installation. It is linked only into UGripper binaries that
need standalone ROS 2-compatible CDR serialization.
