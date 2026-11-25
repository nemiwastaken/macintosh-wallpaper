#!/usr/bin/env bash
# Copy the wallpaper's runtime resources from a macOS IPSW on Linux.
# Requires ipsw >= 3.1, apfs-fuse, and about 15 GB of working space in $TMPDIR.
set -euo pipefail

ipsw_file="${1:?usage: extract.sh <UniversalMac.ipsw> [outdir]}"
out="${2:-$PWD/_mac_extract}"

mkdir -p -- "$out" "${TMPDIR:-/tmp}"
work="$(mktemp -d -- "$out/.extract.XXXXXX")"
mount_dir="$work/mnt"

cleanup() {
    local status=$?
    if mountpoint -q "$mount_dir" && ! fusermount -u "$mount_dir"; then
        printf 'could not unmount %s\n' "$mount_dir" >&2
        status=1
    fi
    rm -rf -- "$work/resources"
    rmdir -- "$mount_dir" "$work" 2>/dev/null || true
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

mkdir -- "$mount_dir" "$work/resources"
ipsw mount fs "$ipsw_file" --mount-point "$mount_dir" --detach
source="$mount_dir/root/System/Library/ExtensionKit/Extensions/WallpaperMacintoshExtension.appex/Contents/Resources"

find "$source" -maxdepth 1 -type f \( -name '*.program' -o -name '*.png' -o -name '*.bin' \) \
    -exec cp -p -t "$work/resources" -- {} +
shopt -s nullglob
for extension in program png bin; do
    files=("$work/resources"/*."$extension")
    if [ "${#files[@]}" -eq 0 ]; then
        printf 'no .%s resources found in %s\n' "$extension" "$source" >&2
        exit 1
    fi
done

rm -rf -- "$out/resources"
mv -T -- "$work/resources" "$out/resources"
printf 'resources: %s\n' "$out/resources"
