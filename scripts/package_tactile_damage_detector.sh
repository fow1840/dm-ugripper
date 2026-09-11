#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "$script_dir/.." && pwd)
output_path=${1:-"$repo_root/tmp/tactile_damage_detector.tar.gz"}
template_dir="$repo_root/tools/tactile_damage_detector"
algorithm_source="$template_dir/tactile_damage_detector.py"

stage_root=$(mktemp -d)
trap 'rm -rf -- "$stage_root"' EXIT

package_root="$stage_root/tactile_damage_detector"
mkdir -p -- "$package_root"
install -m 0755 "$template_dir/detect" "$package_root/detect"
install -m 0755 "$algorithm_source" "$package_root/tactile_damage_detector.py"
install -m 0644 "$template_dir/requirements.txt" "$package_root/requirements.txt"
install -m 0644 "$template_dir/README.md" "$package_root/README.md"

mkdir -p -- "$(dirname -- "$output_path")"
tar -C "$stage_root" -czf "$output_path" tactile_damage_detector
echo "$output_path"
