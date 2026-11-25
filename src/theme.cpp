#include "theme.h"
#include <cmath>

float srgbToLinear(float c) {
    if (c <= 0.04045f) return c / 12.92f;
    return std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float c) {
    if (c <= 0.0031308f) return c * 12.92f;
    return 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

namespace {
// macOS 27 (26B131) AppKit sRGB values, read from the double constants loaded
// by +[NSDynamicSystemColor system*Color] in the dyld shared cache
// (0x18b7edf00…). They differ from the older HIG light palette: systemGreen is
// the lime #63DA38, purple #CC73E1, blue #1BADF8.
struct Rgb { float r, g, b; };
constexpr Rgb kGreenL{0.388235f, 0.854902f, 0.219608f};  // #63DA38
constexpr Rgb kYellowL{1.000000f, 0.800000f, 0.000000f};  // #FFCC00
constexpr Rgb kOrangeL{1.000000f, 0.584314f, 0.000000f};  // #FF9500
constexpr Rgb kRedL{1.000000f, 0.231373f, 0.188235f};     // #FF3B30
constexpr Rgb kPurpleL{0.800000f, 0.450980f, 0.882353f};  // #CC73E1
constexpr Rgb kBlueL{0.105882f, 0.678431f, 0.972549f};    // #1BADF8

// sub_1000139CC takes no appearance argument: the wallpaper resolves the same
// NSColor in both appearances, and makeShading applies the dark k=0.3
// afterwards. The dark spectrum art (olive-green cells) confirms that the
// light components are reused, so there is no separate dark table.

Color fromRgb(const Rgb& c) {
    return {srgbToLinear(c.r), srgbToLinear(c.g), srgbToLinear(c.b), 1.0f};
}
} // namespace

Color schemeColor(ColorScheme s, bool dark) {
    (void)dark; // see above: the appearance only scales the paper in makeShading
    // gray / darkGray are literal constants in the binary (0x100023BD0 / 0x100023BE0),
    // already expressed in linear space.
    static const Color kGray{0.7f, 0.7f, 0.7695f, 1};
    static const Color kDarkGray{0.165f, 0.15f, 0.15f, 1};
    switch (s) {
        case ColorScheme::Gray: return kGray;
        case ColorScheme::DarkGray: return kDarkGray;
        case ColorScheme::Green: return fromRgb(kGreenL);
        case ColorScheme::Yellow: return fromRgb(kYellowL);
        case ColorScheme::Orange: return fromRgb(kOrangeL);
        case ColorScheme::Red: return fromRgb(kRedL);
        case ColorScheme::Purple: return fromRgb(kPurpleL);
        case ColorScheme::Blue: return fromRgb(kBlueL);
        default: return fromRgb(kGreenL);
    }
}

const char* schemeName(ColorScheme s) {
    switch (s) {
        case ColorScheme::Random: return "random";
        case ColorScheme::Spectrum: return "spectrum";
        case ColorScheme::Accent: return "accent";
        case ColorScheme::Gray: return "gray";
        case ColorScheme::DarkGray: return "darkGray";
        case ColorScheme::Green: return "green";
        case ColorScheme::Yellow: return "yellow";
        case ColorScheme::Orange: return "orange";
        case ColorScheme::Red: return "red";
        case ColorScheme::Purple: return "purple";
        case ColorScheme::Blue: return "blue";
    }
    return "green";
}

bool parseScheme(const std::string& n, ColorScheme& out) {
    static const ColorScheme all[] = {
        ColorScheme::Random, ColorScheme::Spectrum, ColorScheme::Accent, ColorScheme::Gray,
        ColorScheme::DarkGray, ColorScheme::Green, ColorScheme::Yellow, ColorScheme::Orange,
        ColorScheme::Red, ColorScheme::Purple, ColorScheme::Blue};
    for (auto s : all)
        if (n == schemeName(s)) { out = s; return true; }
    return false;
}

Color spectrumColor(double seconds) {
    // sub_100014184: t = fmod(time, 60)/60 * 6, then successive lerps through
    // green -> yellow -> orange -> red -> purple -> blue -> green.
    const Color stops[6] = {
        schemeColor(ColorScheme::Green), schemeColor(ColorScheme::Yellow),
        schemeColor(ColorScheme::Orange), schemeColor(ColorScheme::Red),
        schemeColor(ColorScheme::Purple), schemeColor(ColorScheme::Blue)};
    double f = std::fmod(seconds, 60.0);
    if (f < 0) f += 60.0;
    float v = (float)(f / 60.0 * 6.0);
    int i = (int)v;
    float t = v - (float)i;
    Color a = stops[i % 6];
    Color b = stops[(i + 1) % 6];
    return a.mix(b, t);
}

static inline float powSafe(float v, float e) { return std::pow(v < 0 ? 0 : v, e); }

Shading makeShading(const Color& scene, bool dark, float contrast) {
    const float k = dark ? 0.3f : 1.0f;
    const float lift = dark ? 0.09f : 0.26f;   // unk_100022FF8 / +4

    Shading s;
    s.bgBottom = scene * k;
    auto liftChannel = [&](float c) {
        float g = powSafe(c * k, 1.0f / 2.2f);
        return powSafe(g + (1.0f - g) * lift, 2.2f);
    };
    s.bgTop = {liftChannel(scene.r), liftChannel(scene.g), liftChannel(scene.b), 1.0f};
    s.ink = s.bgBottom * contrast;
    return s;
}
