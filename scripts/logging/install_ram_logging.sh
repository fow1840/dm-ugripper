#!/bin/bash
# Validate and atomically publish a recoverable machine-wide logging policy.
set -euo pipefail
[[ $EUID == 0 && $# == 1 ]] || exit 2
exec /usr/bin/python3 "$(dirname "$(readlink -f "$0")")/ram_logging_config.py" install "$1"
