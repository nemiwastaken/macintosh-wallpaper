#!/bin/sh
# Change the running wallpaper without restarting it.
#
#   macintosh-wallpaper-set theme red
#   macintosh-wallpaper-set program Finder
#   macintosh-wallpaper-set paused on
#   macintosh-wallpaper-set                 # print the current settings
#   macintosh-wallpaper-set reset           # forget them: the next start uses the command line
#
# The daemon re-reads $XDG_RUNTIME_DIR/macintosh-wallpaper-control twice a second.
rt="${XDG_RUNTIME_DIR:-/tmp}"
ctl="$rt/macintosh-wallpaper-control"

if [ $# -eq 0 ]; then
    [ -f "$ctl" ] && cat "$ctl" && exit 0
    exit 1
fi

if [ "$1" = reset ]; then
    rm -f "$ctl"
    exit 0
fi

if [ $# -lt 2 ]; then
    echo "usage: macintosh-wallpaper-set theme|program|appearance|presentation|speed|fps|paused VALUE" >&2
    exit 2
fi

key=$1
shift
value=$*

case "$key" in
    theme|program|appearance|presentation|speed|fps|paused) ;;
    *) echo "unknown setting: $key" >&2; exit 2 ;;
esac

# The daemon reads the whole file, so keep the other settings and replace this one.
tmp="$ctl.$$"
{ [ ! -f "$ctl" ] || grep -v "^$key=" "$ctl"; printf '%s=%s\n' "$key" "$value"; } >"$tmp" || exit 1
mv "$tmp" "$ctl"
