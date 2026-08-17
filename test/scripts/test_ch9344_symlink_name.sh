#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
HELPER="$REPO_ROOT/scripts/ugripper_ch9344_symlink_name.sh"
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT HUP INT TERM

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

check_group() {
    route="$1"
    first_num="$2"
    side="$3"
    tty_dir="$TMP_ROOT/devices/$route/${route}:1.0/tty"
    first="ttyCH9344USB$first_num"
    second="ttyCH9344USB$((first_num + 1))"
    third="ttyCH9344USB$((first_num + 2))"

    mkdir -p "$tty_dir"
    : > "$tty_dir/$first"
    : > "$tty_dir/$second"
    : > "$tty_dir/$third"

    actual=$($HELPER "$tty_dir/$first" "$first")
    [ "$actual" = "${side}_gripper" ] || fail "$route first port: $actual"

    actual=$($HELPER "$tty_dir/$second" "$second")
    [ "$actual" = "${side}_encoder" ] || fail "$route second port: $actual"

    if $HELPER "$tty_dir/$third" "$third" >/dev/null 2>&1; then
        fail "$route third port unexpectedly produced a symlink"
    fi
}

reject_group() {
    route="$1"
    first_num="$2"
    tty_dir="$TMP_ROOT/devices/$route/${route}:1.0/tty"
    first="ttyCH9344USB$first_num"

    mkdir -p "$tty_dir"
    : > "$tty_dir/$first"
    if $HELPER "$tty_dir/$first" "$first" >/dev/null 2>&1; then
        fail "$route unexpectedly produced a legacy symlink"
    fi
}

check_group "1-1.4" 0 right
check_group "5-1.4" 8 left
reject_group "1-1.2" 16
reject_group "9-1.4" 24
reject_group "11-1.4" 32

echo "PASS: CH9344 symlink topology mappings"
