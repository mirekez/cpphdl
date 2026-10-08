#!/usr/bin/env bash
# Install only prebuilt shared CIRCT/LLVM libraries; never build LLVM from source.
set -euo pipefail
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
prefix=${1:?CIRCT SDK installation directory required}
if [[ -f "$prefix/lib/cmake/circt/CIRCTConfig.cmake" &&
      -f "$prefix/lib/cmake/mlir/MLIRConfig.cmake" &&
      -f "$prefix/lib/cmake/llvm/LLVMConfig.cmake" ]]; then
  echo "Reusing CIRCT SDK: $prefix"
  exit 0
fi
[[ ! -e "$prefix" && ! -L "$prefix" ]] || {
  echo "error: incomplete SDK at $prefix; existing files left untouched" >&2
  exit 1
}
[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || {
  echo "error: set CPPHDL_CIRCT_PREFIX to a compatible CIRCT 1.75.0 SDK" >&2
  exit 1
}
version=firtool-1.75.0
asset=circt-full-shared-linux-x64.tar.gz
url="https://github.com/llvm/circt/releases/download/$version/$asset"
cache="$script_dir/.cache"
mkdir -p "$cache" "$(dirname -- "$prefix")"
archive="$cache/$asset"
checksum=$(curl --fail --location --silent --show-error --retry 3 "$url.sha256")
[[ $checksum =~ ^[0-9a-fA-F]{64}$ ]] || { echo 'error: invalid CIRCT checksum' >&2; exit 1; }
if [[ ! -f "$archive" ]] || ! printf '%s  %s\n' "$checksum" "$archive" | sha256sum --check --status; then
  curl --fail --location --silent --show-error --retry 3 "$url" -o "$archive.part"
  printf '%s  %s\n' "$checksum" "$archive.part" | sha256sum --check --status
  mv -- "$archive.part" "$archive"
fi
stage=$(mktemp -d "${prefix}.install.XXXXXX")
trap 'rm -rf -- "$stage"' EXIT
tar -xzf "$archive" --strip-components=1 -C "$stage"
for package in llvm mlir circt; do
  test -d "$stage/lib/cmake/$package"
done
mv -- "$stage" "$prefix"
# The installed SDK is sufficient; do not retain a second compressed copy.
rm -- "$archive"
echo "Installed shared CIRCT SDK: $prefix"
