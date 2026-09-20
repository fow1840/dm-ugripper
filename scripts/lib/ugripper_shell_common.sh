#!/bin/bash

ugripper_trim_text() {
    local text="${1:-}"
    text="${text#"${text%%[![:space:]]*}"}"
    text="${text%"${text##*[![:space:]]}"}"
    printf '%s' "$text"
}

ugripper_log() {
    local prefix="${1:-ugripper}"
    local level="${2:-INFO}"
    shift 2 || true
    local error_code=""
    local level_short="I"
    local source_function="${FUNCNAME[1]:-main}"
    local source_line="${BASH_LINENO[0]:-0}"
    local timestamp=""
    local payload=""

    case "${level^^}" in
        TRACE) return 0 ;;
        DEBUG) return 0 ;;
        INFO) level_short="I" ;;
        WARN|WARNING) level_short="W" ;;
        ERROR) level_short="E" ;;
        FATAL|CRITICAL) level_short="E" ;;
        *) return 2 ;;
    esac
    if [ "$level_short" = "E" ] || [ "$level_short" = "F" ]; then
        error_code="${1:-}"
        shift || true
        case "$error_code" in
            UGR-[0-9][0-9]-[0-9][0-9]) ;;
            *) return 2 ;;
        esac
    fi
    payload="[$prefix] $*"
    if [ -n "$error_code" ]; then
        payload="$payload error_code=$error_code"
    fi
    timestamp="$(date '+%s.%6N')"
    printf '[%s][tid %s][%s][%s %s] %s\n' \
        "$timestamp" "$BASHPID" "$level_short" "$source_function" "$source_line" "$payload"
}

ugripper_read_env_value() {
    local env_file="$1"
    local key="$2"
    local line=""
    local current_key=""
    local value=""

    [ -f "$env_file" ] || return 1

    while IFS= read -r line || [ -n "$line" ]; do
        line="$(ugripper_trim_text "$line")"
        [ -n "$line" ] || continue

        case "$line" in
            \#*)
                continue
                ;;
        esac

        if [ "${line#*=}" = "$line" ]; then
            continue
        fi

        current_key="$(ugripper_trim_text "${line%%=*}")"
        value="$(ugripper_trim_text "${line#*=}")"
        if [ "$current_key" != "$key" ]; then
            continue
        fi

        value="${value#\"}"
        value="${value%\"}"
        value="${value#\'}"
        value="${value%\'}"
        printf '%s' "$(ugripper_trim_text "$value")"
        return 0
    done < "$env_file"

    return 0
}

ugripper_resolve_python() {
    local project_root="${1:-}"

    if [ -n "$project_root" ] && [ -x "$project_root/.venv/bin/python3" ]; then
        printf '%s' "$project_root/.venv/bin/python3"
        return 0
    fi

    if command -v uv >/dev/null 2>&1; then
        printf '%s' "uv run python3"
        return 0
    fi

    printf '%s' "python3"
}

ugripper_resolve_first_existing() {
    local candidate=""

    for candidate in "$@"; do
        [ -n "$candidate" ] || continue

        if [ -e "$candidate" ] || [ -L "$candidate" ]; then
            printf '%s' "$candidate"
            return 0
        fi
    done

    printf '%s' "${1:-}"
}

ugripper_resolve_existing_path() {
    ugripper_resolve_first_existing "$@"
}
