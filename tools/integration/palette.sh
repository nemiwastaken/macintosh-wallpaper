#!/bin/sh
# Print the live wallpaper's current colours, or nothing when it is not running.
# This is the whole integration protocol: a pid file and a palette file, both in
# $XDG_RUNTIME_DIR, both written by `macintosh-wallpaper --layer-shell`.
#
#   paper_top=#8ac06a
#   paper_bottom=#7fbf62
#   ink=#33502e
rt="${XDG_RUNTIME_DIR:-/tmp}"

pid=$(cat "$rt/macintosh-wallpaper.pid" 2>/dev/null) || exit 1
case "$pid" in ''|*[!0-9]*) exit 1 ;; esac

# a pid file left by a crash belongs to a reused pid; require the right binary
kill -0 "$pid" 2>/dev/null || exit 1
grep -qz 'macintosh-wallpaper' "/proc/$pid/cmdline" 2>/dev/null || exit 1

# atomically renamed by the daemon, so this is always a whole file
cat "$rt/macintosh-wallpaper-palette" 2>/dev/null
