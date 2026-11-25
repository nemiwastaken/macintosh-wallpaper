#!/bin/sh
# Start/stop the animated wallpaper. amane hides its own picture while the pid is live,
# so stopping this gives the shell's static wallpaper back within a second.
pid_file="${XDG_RUNTIME_DIR:-/tmp}/macintosh-wallpaper.pid"
runtime=${pid_file%/*}
pid=$(cat "$pid_file" 2>/dev/null)
case "$pid" in ''|*[!0-9]*) pid= ;; esac
if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
    kill "$pid"
    # SIGTERM only sets a flag; the daemon sleeps in wl_display_dispatch, so
    # wait a moment and force it. Otherwise the toggle appears to do nothing.
    i=0
    while kill -0 "$pid" 2>/dev/null && [ "$i" -lt 10 ]; do
        sleep 0.2
        i=$((i + 1))
    done
    kill -0 "$pid" 2>/dev/null && kill -9 "$pid" 2>/dev/null
    rm -f "$pid_file" "$runtime/macintosh-wallpaper-palette" "$runtime/macintosh-wallpaper-frame.png"
else
    # the niri startup runs it on the discrete GPU too; keep both paths identical
    if command -v prime-run >/dev/null 2>&1; then
        setsid prime-run macintosh-wallpaper --layer-shell >/dev/null 2>&1 &
    else
        setsid macintosh-wallpaper --layer-shell >/dev/null 2>&1 &
    fi
fi
