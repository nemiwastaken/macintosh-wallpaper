#include "overlay.h"
#include <algorithm>
#include <cstdio>

bool Overlay::init(const std::string& fontPath) {
    if (!font_.load(fontPath)) return false;
    bmp_.reset(kW, kH);
    return true;
}

int Overlay::scaleFor(int fw, int fh) {
    // Cap at 2x: a tiled 1852x1052 window used to blow the panel up to 1440x906.
    int s = std::min(fw / (kW + 40), fh / (kH + 40));
    return std::clamp(s, 1, 2);
}

static void frameRect(Bitmap& b, int x, int y, int w, int h) {
    for (int i = x; i < x + w; i++) {
        if (i >= 0 && i < b.w) {
            if (y >= 0 && y < b.h) b.at(i, y) = 1;
            if (y + h - 1 >= 0 && y + h - 1 < b.h) b.at(i, y + h - 1) = 1;
        }
    }
    for (int j = y; j < y + h; j++) {
        if (j < 0 || j >= b.h) continue;
        if (x >= 0 && x < b.w) b.at(x, j) = 1;
        if (x + w - 1 >= 0 && x + w - 1 < b.w) b.at(x + w - 1, j) = 1;
    }
}

void Overlay::build(const GuiModel& m) {
    bmp_.reset(kW, kH); // paper = panel, ink = white text
    font_.draw(bmp_, "Macintosh Wallpaper - Settings", 18, 12);

    char speed[32], fps[16];
    snprintf(speed, sizeof speed, "%.2fx", m.speed);
    snprintf(fps, sizeof fps, "%d", m.fps);
    const std::string labels[kRows] = {"Program", "Theme", "Appearance", "Presentation",
                                      "Speed", "FPS", "Paused"};
    const std::string values[kRows] = {m.program,
                                       m.theme,
                                       m.appearance,
                                       m.presentation,
                                       speed,
                                       fps,
                                       m.paused ? "yes" : "no"};
    for (int i = 0; i < kRows; i++) {
        int y = kRowY0 + i * kRowH;
        if (i == m.row) frameRect(bmp_, 10, y - 5, kW - 20, 22);
        std::string text = labels[i];
        int pad = 150 - font_.width(text);
        if (pad > 0) text.append((size_t)pad / 4, ' ');
        text += (i == m.row ? "< " : "  ") + values[i] + (i == m.row ? " >" : "  ");
        font_.draw(bmp_, text, 20, y);
    }
    font_.draw(bmp_, "Up/Down: row   Left/Right: value   Space: pause", 18, 238);
    font_.draw(bmp_, "Tab: hide   Esc: close   Mouse: click a row side", 18, 254);
    font_.draw(bmp_, "Desktop: --layer-shell --program " + m.program + " --theme " + m.theme,
               18, 272);
}

void Overlay::draw(Renderer& r, const GuiModel& m, int fw, int fh) {
    if (fw <= 0 || fh <= 0) return;
    build(m);
    rgba_.resize((size_t)kW * kH * 4);
    for (int y = 0; y < kH; y++)
        for (int x = 0; x < kW; x++) {
            uint8_t* p = &rgba_[((size_t)y * kW + x) * 4];
            if (bmp_.at(x, y)) {
                p[0] = p[1] = p[2] = 255;
                p[3] = 255;
            } else {
                p[0] = p[1] = p[2] = 0;
                p[3] = 200;
            }
        }
    r.drawOverlay(rgba_.data(), kW, kH, scaleFor(fw, fh));
}

int Overlay::rowAt(int px, int py, int fw, int fh) const {
    int s = scaleFor(fw, fh);
    int ox = (fw - kW * s) / 2, oy = (fh - kH * s) / 2;
    if (px < ox || py < oy) return -1;
    int bx = (px - ox) / s, by = (py - oy) / s;
    if (bx < 0 || bx >= kW || by < 0 || by >= kH) return -1;
    for (int i = 0; i < kRows; i++) {
        int y = kRowY0 + i * kRowH;
        if (by >= y - 6 && by < y + 17) return i;
    }
    return -1;
}
