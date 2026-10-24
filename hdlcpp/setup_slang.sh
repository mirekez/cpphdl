#!/usr/bin/env bash
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
revision="$(tr -d '\r\n' < "$here/slang-revision.txt")"
[[ "$revision" =~ ^[0-9a-f]{40}$ ]] || { printf 'Invalid Slang revision\n' >&2; exit 1; }
source_dir="$here/slang"
if [[ ! -e "$source_dir" ]]; then
    git init -q "$source_dir"
    git -C "$source_dir" remote add origin https://github.com/MikePopoloski/slang.git
fi
if [[ ! -e "$source_dir/.git" ]]; then
    printf 'Expected a Git checkout at %s; refusing to overwrite it\n' "$source_dir" >&2
    exit 1
fi
if [[ -n "$(git -C "$source_dir" status --porcelain)" ]]; then
    printf 'Slang checkout has local changes; preserve them before running setup\n' >&2
    exit 1
fi
if ! git -C "$source_dir" cat-file -e "$revision^{commit}" 2>/dev/null; then
    git -C "$source_dir" fetch --depth=1 origin "$revision"
fi
git -C "$source_dir" checkout --detach "$revision"
test "$(git -C "$source_dir" rev-parse HEAD)" = "$revision"
printf 'Slang pinned to %s\n' "$revision"
