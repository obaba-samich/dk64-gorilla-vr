#!/usr/bin/env bash
# Apply the VR patches to the RT64 and Plume submodules. Safe to run again: a patch that is
# already in place is left alone. Refuses to touch a submodule with other local changes.
set -euo pipefail
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
rt64="$repo/lib/rt64"
plume="$rt64/src/contrib/plume"
patches="$repo/third_party/vr-dependencies"

apply() {
    local dir="$1" patch="$2" name="$3"
    if git -C "$dir" apply --reverse --check "$patch" >/dev/null 2>&1; then
        echo "$name: VR patch already applied"
        return
    fi
    if [[ -n "$(git -C "$dir" status --porcelain --untracked-files=no --ignore-submodules=all)" ]]; then
        echo "$name has local changes that aren't the VR patch, not touching it" >&2
        exit 1
    fi
    git -C "$dir" apply "$patch"
    echo "$name: VR patch applied"
}

apply "$plume" "$patches/plume-vr.patch" plume
apply "$rt64" "$patches/rt64-vr.patch" rt64
for p in "$patches"/rt64-*.extra.patch; do
    [[ -e "$p" ]] || continue
    if git -C "$rt64" apply --reverse --check "$p" >/dev/null 2>&1; then
        echo "rt64: $(basename "$p") already applied"
    else
        git -C "$rt64" apply "$p"
        echo "rt64: $(basename "$p") applied"
    fi
done
