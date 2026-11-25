# macintosh-wallpaper

A Linux port of the macOS “Macintosh” dynamic wallpaper. It plays all 15
original scenes with their bitmap artwork, animated cursors, and interactive
controls. Run it as a Wayland wallpaper or preview it in a window. Both
wallpaper and screensaver presentations support light and dark themes.

## Quick start

Run these commands from the repository root.

### 1. Get the assets

Provide a local directory of wallpaper resources you are authorized to use.
The port reads scene scripts (`.program`), sprites (`.png`), and bitmap fonts
and cursors (`.bin`). Supply the directory with `--assets`. The install target
contains the executable and its shaders. Wallpaper resources are supplied separately.
See the [IPSW extraction notes](docs/RECONSTRUCTION.md#use-a-local-ipsw) for the
optional extraction tools.

### 2. Build

On Arch Linux, the build uses `cmake`, `gcc`, `make`, `pkgconf`, `glfw`,
`libepoxy`, `libpng`, and `libglvnd`, with a driver that supports OpenGL 3.3. Add
`wayland` for wallpaper mode, which requires a compositor with
`wlr-layer-shell` support.

```sh
cmake -S . -B build
cmake --build build -j
```

### 3. Run

Choose one of these commands.

```sh
resources="/path/to/wallpaper/resources"
./build/macintosh-wallpaper --assets "$resources" --layer-shell
./build/macintosh-wallpaper --assets "$resources" --gui
```

## Settings

For example, start the wallpaper with the blue theme in light mode.

```sh
./build/macintosh-wallpaper --assets "$resources" --layer-shell --theme blue --appearance light
```

Add `--presentation screensaver --cycle` to rotate through all scenes in
screensaver mode. Use `--list-programs`, `--list-themes`, or `--help` to
see the available options.

In the preview, use **↑/↓** to select a row, **←/→** to change its value,
**Space** to pause, **Tab** to toggle the panel, and **Esc** to close the
window. Changes in the preview apply to that window. To change an already
running wallpaper, use the control script.

```sh
tools/integration/control.sh theme red
```

See [desktop integration](docs/INTEGRATION.md) for live settings and palette hooks.

## Fidelity

The port was checked against macOS captures for camera angles and colour.
The camera-dependent part of zoom is approximate, and MacWrite text input is
scripted. Run `./build/macintosh-wallpaper --assets "$resources" --selftest` in a
graphical session to check scene scripts, controls, cursors, and rendering.

See the [reconstruction notes](docs/RECONSTRUCTION.md) for rendering and
interaction details.

## License

The project's original code is licensed under the
[GNU General Public License version 3.0](LICENSE).

The Wayland protocol definitions in `protocols/` retain the licenses included
in their files. Apple's assets and shader material recovered from Apple's AIR
remain outside the scope of the project's GPL grant.
