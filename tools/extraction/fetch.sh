#!/usr/bin/env bash
# Download macOS 27.2.1 (26B131) from Apple's CDN and install its wallpaper
# resources in assets/resources/.
#
# usage: fetch.sh [--device MAC] [--build BUILD] [--url-only] [--keep]
#   --device    look up an image for a different target model via ipsw.me
#   --build     select a build (lookup defaults to MacBookPro18,1)
#   --url-only  print the .ipsw URL and exit (no download)
#   --keep      keep the .ipsw after extracting
#
# Needs: curl, python3, ipsw, apfs-fuse (or IPSW_APFS_FUSE_PATH).
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"

device=""
build=""
url_only=0
keep=0
while [ $# -gt 0 ]; do
    case "$1" in
        --device) device="$2"; shift 2 ;;
        --build) build="$2"; shift 2 ;;
        --url-only) url_only=1; shift ;;
        --keep) keep=1; shift ;;
        -h|--help) sed -n '2,/^set /{ /^#/p; }' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

pinned_build="26B131"
if [ -z "$device" ] && { [ -z "$build" ] || [ "$build" = "$pinned_build" ]; }; then
    url="https://updates.cdn-apple.com/2026FallFCS/c202a724-db7e-4d56-917b-110aa0226507/UniversalMac_27.2.1_26B131_Restore.ipsw"
    version="27.2.1"
    build_id="$pinned_build"
    bytes=25595954706
else
    device="${device:-MacBookPro18,1}"
    if ! read -r url version build_id bytes < <(python3 - "$device" "$build" <<'PY'
import json, sys, urllib.request

device, want = sys.argv[1], sys.argv[2]
with urllib.request.urlopen(f"https://api.ipsw.me/v4/device/{device}?type=ipsw") as r:
    firmwares = json.load(r)["firmwares"]

if want:
    firmwares = [f for f in firmwares if f["buildid"] == want]
    if not firmwares:
        sys.exit(f"no image with build {want}")

newest = max(firmwares, key=lambda f: [int(p) for p in f["version"].split(".")])
print(newest["url"], newest["version"], newest["buildid"], newest["filesize"])
PY
    ); then
        echo "cannot resolve an image for $device${build:+ (build $build)}" >&2
        exit 1
    fi
fi

printf 'macOS %s (%s), %s GB\n%s\n' "$version" "$build_id" \
       "$(LC_ALL=C awk -v b="$bytes" 'BEGIN { printf "%.1f", b / 1e9 }')" "$url" >&2

if [ "$url_only" = 1 ]; then
    echo "$url"
    exit 0
fi

cache="${XDG_CACHE_HOME:-$HOME/.cache}/macintosh-wallpaper"
mkdir -p "$cache"
ipsw_file="$cache/$(basename "$url")"

if [ "$(stat -c %s "$ipsw_file" 2>/dev/null)" != "$bytes" ]; then
    partial="$ipsw_file.part"
    if [ "$(stat -c %s "$partial" 2>/dev/null)" != "$bytes" ]; then
        echo "downloading to $partial (resumable)" >&2
        curl -fL --progress-bar -C - -o "$partial" "$url"
    fi
    if [ "$(stat -c %s "$partial" 2>/dev/null)" != "$bytes" ]; then
        echo "unexpected IPSW size, expected $bytes bytes: $partial" >&2
        exit 1
    fi
    mv -f "$partial" "$ipsw_file"
fi

# Give ipsw room for its temporary image on systems with a small /tmp.
export TMPDIR="${TMPDIR:-$cache}"
"$here/extract.sh" "$ipsw_file" "$repo/assets"

[ "$keep" = 1 ] || rm -f "$ipsw_file"
echo "resources ready: $repo/assets/resources" >&2
