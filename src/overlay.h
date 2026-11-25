#pragma once
#include "font.h"
#include "image.h"
#include "renderer.h"
#include <string>

// Small on-screen settings panel for the GLFW window. Keyboard driven
// (Up/Down row, Left/Right value, Space pause) plus mouse: click the left or
// right half of a row to step its value. It is a 1-bit bitmap (the project's
// own Chicago12 font) blown up with nearest-neighbour sampling.
struct GuiModel {
    int row = 0;
    std::string program, theme, appearance, presentation;
    float speed = 1.0f;
    int fps = 30;
    bool paused = false;
};

class Overlay {
public:
    bool init(const std::string& fontPath);
    void draw(Renderer& r, const GuiModel& m, int fw, int fh);
    // Row under a framebuffer pixel, or -1. x < panel centre means "previous".
    int rowAt(int px, int py, int fw, int fh) const;

private:
    static constexpr int kW = 480, kH = 302;
    static constexpr int kRowY0 = 48, kRowH = 26, kRows = 7;
    static int scaleFor(int fw, int fh);
    void build(const GuiModel& m);

    BitmapFont font_;
    Bitmap bmp_;
    std::vector<uint8_t> rgba_;
};
