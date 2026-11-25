#pragma once
#include <cstdint>
#include <string>
#include <vector>

// 1-bit bitmap: one byte per pixel, 0 = paper, 1 = ink.
struct Sprite;

struct Bitmap {
    int w = 0, h = 0;
    std::vector<uint8_t> px;

    void reset(int width, int height) {
        w = width;
        h = height;
        px.assign((size_t)w * h, 0);
    }
    inline uint8_t& at(int x, int y) { return px[(size_t)y * w + x]; }
    inline uint8_t at(int x, int y) const { return px[(size_t)y * w + x]; }
    void blit(const Bitmap& src, int ox, int oy);
    // Blit one sprite frame honouring transparency: opaque pixels replace the
    // destination (paper or ink), transparent ones leave it untouched. This is
    // what the widget bitmaps need (a normal `blit` only ever sets ink).
    void blitSprite(const Sprite& s, int frame, int ox, int oy);
};

// Sprite strip loaded from an RGBA PNG; frames are laid out horizontally.
struct Sprite {
    int frameW = 0, frameH = 0, frames = 0;
    std::vector<uint8_t> ink;    // frames * frameW * frameH, 1 = black pixel
    std::vector<uint8_t> opaque; // 1 = pixel painted (alpha > 127)

    int clampFrame(int i) const {
        if (!frames) return 0;
        i %= frames;
        if (i < 0) i += frames;
        return i;
    }
    const uint8_t* frame(int i) const {
        if (!frames) return nullptr;
        return ink.data() + (size_t)clampFrame(i) * frameW * frameH;
    }
    const uint8_t* alpha(int i) const {
        if (!frames) return nullptr;
        return opaque.data() + (size_t)clampFrame(i) * frameW * frameH;
    }
};

Bitmap loadMask(const std::string& path);
Sprite loadSprite(const std::string& path, int frameW, int frameH);
bool pngSize(const std::string& path, int& w, int& h);
// Write a 24-bit RGB PNG (for --dump-frame).
bool writePng(const std::string& path, int w, int h, const uint8_t* rgb);
