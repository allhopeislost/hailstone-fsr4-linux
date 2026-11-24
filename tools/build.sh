#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dll="${FSR4_DLL_DIR:-${1:-}}"
hailstone="${HAILSTONE_CLIENT_DIR:-$root/../hailstone-client}"
jobs="${FSR4_BUILD_JOBS:-4}"

git -C "$root" submodule update --init --recursive runtime

if [[ -n "$dll" ]]; then
  python3 "$root/tools/prepare_assets.py" --dll "$dll"
else
  python3 "$root/tools/prepare_assets.py"
fi

cmake -S "$root" -B "$root/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DHAILSTONE_CLIENT_DIR="$hailstone" \
  -DFSR4_BUILD_JOBS="$jobs"
cmake --build "$root/build" --target fsr4 --parallel "$jobs"
cmake --build "$root/build" --target fsr4-check --parallel "$jobs"
python3 "$root/tools/check_package.py" "$root/build/dist"

printf 'Package: %s\n' "$root/build/dist"
