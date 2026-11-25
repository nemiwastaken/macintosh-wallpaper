# Reconstruction notes

## Use a local IPSW

[Build the port](../README.md#2-build), then run the extractor from the
repository root with the path to an image you are authorized to use for this
purpose. The extractor uses `ipsw` 3.1 or newer and `apfs-fuse` to mount the
FileSystem DMG. It copies `.program`, `.png`, and `.bin` files to
`_mac_extract/resources/` by default. Allow about 15 GB of working space in
`TMPDIR`.

```sh
tools/extraction/extract.sh /path/to/UniversalMac.ipsw
./build/macintosh-wallpaper --assets "$PWD/_mac_extract/resources" --gui
```

The optional `tools/extraction/fetch.sh` helper downloads the macOS 27.2.1
restore image (build `26B131`, 25.6 GB) from Apple's CDN and extracts the same
files to `assets/resources/`. It uses `curl`. Pass `--keep` to retain the
IPSW or `--url-only` to print its URL. See Apple's
[macOS license](https://www.apple.com/legal/sla/docs/macOS27.pdf) for its terms
on copying and using restore images and bundled resources.

## Rendering

- The original 512×342 one-bit screens are drawn with one instanced quad per
  pixel. The five AIR shaders were recovered from `default.metallib` through
  LLVM IR and ported to GLSL. The recovered `Uniforms` layout is 208 bytes.
- Pixel vertices extend ±0.45 from the centre. Shadow vertices extend ±1.125 at
  z = −0.45. The world position uses `scroll − texel − 1`.
- The field of view is 140, or 180 when the aspect ratio is outside the range
  0.5–2. The camera rolls around Z by `camera.x` and tilts around X by
  `camera.y`. The projected pixel angles were compared with macOS captures.
- The gradient is computed in linear light. Wallpaper and screensaver
  contrast values are 0.666 and 0.2, with lifts of 0.26 and 0.09. The gradient
  uses a `pow(…, 1/2.2)` curve. The port renders to a floating-point target and
  encodes the final 8-bit sRGB output with 1-LSB dithering to limit banding.
- Spectrum runs on a 60-second cycle recovered from `sub_100014184`. The
  wallpaper presentation uses a scale dissolve. The screensaver uses a
  directional wipe.

## Scenes and interactions

The port runs the original `.program` files, including camera moves, scrolling,
zoom, subroutines, and simultaneous and named asynchronous animations. It uses
the original sprites and NFNT fonts (Chicago12, Geneva12, Geneva9, and Monaco9).
The System 1 and System 6 panels display live time and date.

The built-in tests exercise script commands, sprite placement, all 17 control
types, and cursor resource mappings. The original interactions include
sliders, radio groups, menus, the puzzle, Finder icon dragging, canvas drawing,
and text selection. Watch, Hand, Ball, and Clarus use their original
`acur_*.bin` frame lists.

The zoom multiplier `1 + 2·zoom` is implemented. The camera-dependent part of
zoom remains approximate. MacWrite's text is typed by its scene script, and
the port supports moving the caret and selecting text with the mouse.
