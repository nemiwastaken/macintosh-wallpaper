# Desktop integration

The wallpaper uses `wlr-layer-shell` to create a background surface on each
output. See the [quick start](../README.md#quick-start) for build and launch
instructions.

## Toggle shortcut on niri

Run these commands from the repository root. Make sure `~/.local/bin` is on
the PATH inherited by niri.

```sh
mkdir -p ~/.local/bin
ln -sfn "$PWD/build/macintosh-wallpaper" ~/.local/bin/macintosh-wallpaper
ln -sfn "$PWD/tools/wallpaper-toggle.sh" ~/.local/bin/macintosh-wallpaper-toggle
```

Add the binding inside your existing `binds` block in the niri config.

```kdl
Mod+Alt+Shift+W { spawn "macintosh-wallpaper-toggle"; }
```

## Live settings

Run these commands while the wallpaper is running.

```sh
tools/integration/control.sh theme red
tools/integration/control.sh program Finder
tools/integration/control.sh paused on
tools/integration/control.sh
tools/integration/control.sh reset
```

With no arguments, the script prints the overrides saved in
`$XDG_RUNTIME_DIR/macintosh-wallpaper-control`. The wallpaper reads that file
twice a second and applies changes in place. Overrides survive a wallpaper
restart in the same session. The `reset` command clears the file so the next
launch uses its command-line settings.

Each line is a `key=value` pair. Available keys are `theme`, `program`,
`appearance`, `presentation`, `speed`, `fps`, and `paused`.

## Shell palette

A running wallpaper publishes these files in `$XDG_RUNTIME_DIR` (or `/tmp`
when it is unset). The wallpaper removes its own files on graceful exit.

| File | Contents |
| --- | --- |
| `macintosh-wallpaper.pid` | Process ID used to check whether the wallpaper is running |
| `macintosh-wallpaper-palette` | sRGB hex colours for `paper_top`, `paper_bottom`, and `ink` |
| `macintosh-wallpaper-frame.png` | A 24×14 swatch of the paper gradient and ink |

The palette and swatch are refreshed when the colours change, about every two
seconds in spectrum mode. A shell can hide its static background while the
wallpaper is running and use the live palette for accents or translucent
panels. The [palette helper](../tools/integration/palette.sh) checks the
process before printing its colours.

## Hybrid NVIDIA systems

If you use NVIDIA PRIME offload for the wallpaper, launch it through
`prime-run`. The toggle script uses `prime-run` when it is on PATH.

A Vulkan shell may also need an explicit driver. On a system with the NVIDIA
ICD at the path below, this example starts Amane on that driver.

```kdl
spawn-at-startup "env" "VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json" "prime-run" "amane" "run"
```
