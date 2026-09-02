#!/usr/bin/env bash
set -euo pipefail

# Fast ARM deployment helper. Build-mode selection may skip compilation, but a
# deployment request always synchronizes the selected packages and runs dpkg.

SCRIPT_NAME="$(basename "$0")"
TARGET_HOST="${TARGET_HOST:-192.168.2.240}"
TARGET_USER="${TARGET_USER:-ubuntu}"
TARGET_PASSWORD="${TARGET_PASSWORD:-}"
REMOTE_DIR="${REMOTE_DIR:-/tmp/ugripper-deb-install}"
KNOWN_HOSTS_PATH="${KNOWN_HOSTS_PATH:-/tmp/codex_known_hosts_3588ether}"
DEB_PATH=""
UPDATER_PATH=""
SKIP_UPDATER=0
SHOW_PROGRESS=0
INSTALL_ONLY=0
QUICK_PACKAGE=0
FULL_CLEAN=0
FORCE_UPDATER=0
AUTO_RELEASE_RESULT=""
UGRIPPER_BUILD_MODE="skip"
UPDATER_BUILD_MODE="skip"

usage() {
    cat >&2 <<EOF
Usage: ${SCRIPT_NAME} [options]

Default: use the repository source-manifest baseline to select full, quick, or
skip independently for the main and updater packages, then deploy.

Options:
  --host <host>             Target host (default: ${TARGET_HOST})
  --user <user>             SSH user (default: ${TARGET_USER})
  --password <password>     SSH/sudo password (or TARGET_PASSWORD)
  --remote-dir <directory>  Remote staging directory (default: ${REMOTE_DIR})
  --updater <path>          Updater .deb to install as well
  --skip-updater            Install only the main package
  --install-only            Skip ARM compilation and packaging
  --quick-package           Override baseline: repackage existing ARM payload
  --full-clean              Override baseline: force a clean ARM build
  --rebuild-updater         Rebuild updater even when its source is unchanged
  --progress                Show rsync progress
  -h, --help                Show this help
EOF
}

require_tool() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "Required command not found: $1" >&2
        exit 1
    }
}

shell_quote() {
    local value="$1"
    printf "'%s'" "${value//\'/\'\\\'\'}"
}

newest_or_empty() {
    local pattern="$1"
    local candidate
    candidate="$(ls -t $pattern 2>/dev/null | head -n1 || true)"
    printf '%s' "$candidate"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --deb) [[ $# -ge 2 ]] || { usage; exit 2; }; DEB_PATH="$2"; shift 2 ;;
        --updater) [[ $# -ge 2 ]] || { usage; exit 2; }; UPDATER_PATH="$2"; shift 2 ;;
        --skip-updater) SKIP_UPDATER=1; shift ;;
        --install-only) INSTALL_ONLY=1; shift ;;
        --quick-package) QUICK_PACKAGE=1; shift ;;
        --full-clean) FULL_CLEAN=1; shift ;;
        --rebuild-updater) FORCE_UPDATER=1; shift ;;
        --host) [[ $# -ge 2 ]] || { usage; exit 2; }; TARGET_HOST="$2"; shift 2 ;;
        --user) [[ $# -ge 2 ]] || { usage; exit 2; }; TARGET_USER="$2"; shift 2 ;;
        --password) [[ $# -ge 2 ]] || { usage; exit 2; }; TARGET_PASSWORD="$2"; shift 2 ;;
        --remote-dir) [[ $# -ge 2 ]] || { usage; exit 2; }; REMOTE_DIR="$2"; shift 2 ;;
        --progress) SHOW_PROGRESS=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *)
            if [[ -z "$DEB_PATH" && "$1" != -* ]]; then
                DEB_PATH="$1"
                shift
            else
                echo "Unknown argument: $1" >&2
                usage
                exit 2
            fi
            ;;
    esac
done

require_tool ssh
require_tool rsync
if [[ -n "$TARGET_PASSWORD" ]]; then require_tool sshpass; fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ "$INSTALL_ONLY" -eq 0 ]]; then
    if [[ "$QUICK_PACKAGE" -eq 1 ]]; then
        UGRIPPER_BUILD_MODE="quick"
    elif [[ "$FULL_CLEAN" -eq 1 ]]; then
        UGRIPPER_BUILD_MODE="full"
    else
        echo "==> Checking release source baseline"
        AUTO_RELEASE_RESULT="$(cd "$REPO_ROOT" && bash .codex/skills/auto-release-deb/scripts/auto_release_deb.sh --no-files)"
        printf '%s\n' "$AUTO_RELEASE_RESULT"
        UGRIPPER_BUILD_MODE="$(printf '%s\n' "$AUTO_RELEASE_RESULT" | sed -n 's/^Ugripper build mode: //p')"
        UPDATER_BUILD_MODE="$(printf '%s\n' "$AUTO_RELEASE_RESULT" | sed -n 's/^Updater build mode: //p')"
        [[ -n "$UGRIPPER_BUILD_MODE" && -n "$UPDATER_BUILD_MODE" ]] || {
            echo "Failed to parse auto-release build modes" >&2
            exit 1
        }
    fi

    case "$UGRIPPER_BUILD_MODE" in
        full)
            echo "==> Building ARM package: full"
            (cd "$REPO_ROOT" && ./scripts/build_arm_deb_in_pp_arm_dev.sh)
            ;;
        quick)
            echo "==> Building ARM package: quick"
            (cd "$REPO_ROOT" && ./scripts/build_arm_deb_in_pp_arm_dev.sh --quick)
            ;;
        skip)
            echo "==> Main package source unchanged; skip build"
            ;;
        *)
            echo "Unsupported Ugripper build mode: $UGRIPPER_BUILD_MODE" >&2
            exit 1
            ;;
    esac

    if [[ "$SKIP_UPDATER" -eq 0 && "$UPDATER_BUILD_MODE" == "full" ]]; then
        FORCE_UPDATER=1
    fi
fi
cd "$REPO_ROOT"

if [[ -z "$DEB_PATH" ]]; then
    DEB_PATH="$(newest_or_empty "./ugripper_*_arm64.deb")"
fi
[[ -n "$DEB_PATH" && -f "$DEB_PATH" ]] || { echo "Main ARM deb not found" >&2; exit 1; }
if [[ "$SKIP_UPDATER" -eq 0 && -z "$UPDATER_PATH" ]]; then
    UPDATER_PATH="$(newest_or_empty "./build/package/updater/*_all.deb")"
fi
if [[ "$SKIP_UPDATER" -eq 0 && ( "$FORCE_UPDATER" -eq 1 || ! -f "$UPDATER_PATH" ) ]]; then
    echo "==> Building updater package"
    (cd "$REPO_ROOT" && ./usb_updater_build.sh)
    UPDATER_PATH="$(newest_or_empty "./build/package/updater/*_all.deb")"
fi
if [[ "$SKIP_UPDATER" -eq 0 && ! -f "$UPDATER_PATH" ]]; then
    echo "Updater deb not found; use --skip-updater or --updater <path>" >&2
    exit 1
fi

DEB_PATH="$(realpath "$DEB_PATH")"
DEB_NAME="$(basename "$DEB_PATH")"
TARGET="${TARGET_USER}@${TARGET_HOST}"
REMOTE_MAIN="${REMOTE_DIR}/${DEB_NAME}"
SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile="${KNOWN_HOSTS_PATH}" -o Compression=no -c aes128-gcm@openssh.com)

ssh_cmd() {
    if [[ -n "$TARGET_PASSWORD" ]]; then
        SSHPASS="$TARGET_PASSWORD" sshpass -e ssh "${SSH_OPTS[@]}" "$TARGET" "$@"
    else
        ssh "${SSH_OPTS[@]}" "$TARGET" "$@"
    fi
}

rsync_file() {
    local source="$1" remote_path="$2"
    local progress=()
    [[ "$SHOW_PROGRESS" -eq 1 ]] && progress=(--info=progress2)
    if [[ -n "$TARGET_PASSWORD" ]]; then
        SSHPASS="$TARGET_PASSWORD" rsync --checksum --partial --inplace --whole-file "${progress[@]}" \
            -e "sshpass -e ssh ${SSH_OPTS[*]}" "$source" "$TARGET:$remote_path"
    else
        rsync --checksum --partial --inplace --whole-file "${progress[@]}" \
            -e "ssh ${SSH_OPTS[*]}" "$source" "$TARGET:$remote_path"
    fi
}

echo "==> Target:  $TARGET"
echo "==> Main:    $DEB_PATH"
[[ "$SKIP_UPDATER" -eq 1 ]] || echo "==> Updater: $(realpath "$UPDATER_PATH")"

ssh_cmd "mkdir -p $(shell_quote "$REMOTE_DIR")"

start_ns="$(date +%s%N)"
rsync_file "$DEB_PATH" "$REMOTE_MAIN" &
main_pid=$!
updater_remote=""
if [[ "$SKIP_UPDATER" -eq 0 ]]; then
    UPDATER_PATH="$(realpath "$UPDATER_PATH")"
    updater_remote="${REMOTE_DIR}/$(basename "$UPDATER_PATH")"
    rsync_file "$UPDATER_PATH" "$updater_remote" &
    updater_pid=$!
fi
wait "$main_pid"
[[ "$SKIP_UPDATER" -eq 1 ]] || wait "$updater_pid"
transfer_ms=$(( ($(date +%s%N) - start_ns) / 1000000 ))
echo "==> Transfer completed in ${transfer_ms} ms"

main_literal="$(shell_quote "$REMOTE_MAIN")"
updater_literal="$(shell_quote "$updater_remote")"
if [[ -n "$TARGET_PASSWORD" ]]; then
    password_literal="$(shell_quote "$TARGET_PASSWORD")"
    sudo_prefix="printf '%s\\n' ${password_literal} | sudo -S -p ''"
else
    sudo_prefix="sudo"
fi

install_cmd="set -e; ${sudo_prefix} dpkg -i ${main_literal};"
if [[ "$SKIP_UPDATER" -eq 0 ]]; then
    install_cmd+=" ${sudo_prefix} dpkg -i ${updater_literal};"
fi
install_cmd+=" dpkg-query -W -f='package=\${Package}\\nversion=\${Version}\\narchitecture=\${Architecture}\\n' ugripper; systemctl is-active ugripper.service || true"
ssh_cmd "$install_cmd"
echo "==> Install completed."
