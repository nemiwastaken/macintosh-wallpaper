#pragma once
#include <array>
#include <string>

// Recovered ColorScheme enum from the Swift metadata.
enum class ColorScheme {
    Random = 0,
    Spectrum,
    Accent,
    Gray,
    DarkGray,
    Green,
    Yellow,
    Orange,
    Red,
    Purple,
    Blue,
};

struct Color {
    float r = 0, g = 0, b = 0, a = 1;
    Color operator*(float k) const { return {r * k, g * k, b * k, a}; }
    Color mix(const Color& o, float t) const {
        return {r + (o.r - r) * t, g + (o.g - g) * t, b + (o.b - b) * t, 1};
    }
};

// sRGB (0..1) -> linear; the extension resolves NSColor in `kCGColorSpaceLinearSRGB`.
float srgbToLinear(float c);
// linear -> sRGB, for --dump-frame and for the self-test.
float linearToSrgb(float c);

// One system colour in linear sRGB. `dark` selects the dark-appearance variant,
// which is what AppKit resolves when the wallpaper runs in dark mode.
Color schemeColor(ColorScheme s, bool dark = false);
const char* schemeName(ColorScheme s);
bool parseScheme(const std::string& name, ColorScheme& out);

// spectrum/random: continuous 60 s rainbow through green, yellow, orange, red,
// purple, blue (recovered from sub_100014184), interpolated in linear space.
Color spectrumColor(double seconds);

// Background / ink colours exactly as assembled in PixelsLayer.draw (sub_100008420):
//   bottom = scene * k
//   top    = pow( pow(bottom,1/2.2) + (1-pow(bottom,1/2.2)) * lift, 2.2 )
//   ink    = bottom * contrast
struct Shading {
    Color ink;
    Color bgTop;
    Color bgBottom;
};
Shading makeShading(const Color& scene, bool darkAppearance, float contrast);
inline Shading makeShading(const Color& scene, bool darkAppearance) {
    return makeShading(scene, darkAppearance, darkAppearance ? 0.666f : 0.666f);
}
