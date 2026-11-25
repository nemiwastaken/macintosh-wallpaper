#include "image.h"
#include <cstdio>
#include <png.h>
#include <cstring>

namespace {
struct Png {
    png_structp p = nullptr;
    png_infop i = nullptr;
    FILE* f = nullptr;
    ~Png() {
        if (p) png_destroy_read_struct(&p, i ? &i : nullptr, nullptr);
        if (f) fclose(f);
    }
};

bool openPng(const std::string& path, Png& g, int& w, int& h) {
    g.f = fopen(path.c_str(), "rb");
    if (!g.f) return false;
    g.p = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!g.p) return false;
    g.i = png_create_info_struct(g.p);
    if (!g.i) return false;
    if (setjmp(png_jmpbuf(g.p))) return false;
    png_init_io(g.p, g.f);
    png_read_info(g.p, g.i);
    png_set_expand(g.p);
    png_set_strip_16(g.p);
    png_set_gray_to_rgb(g.p);
    png_set_tRNS_to_alpha(g.p);
    png_set_add_alpha(g.p, 0xFF, PNG_FILLER_AFTER);
    png_read_update_info(g.p, g.i);
    w = (int)png_get_image_width(g.p, g.i);
    h = (int)png_get_image_height(g.p, g.i);
    return true;
}

std::vector<uint8_t> readRgba(Png& g, int w, int h) {
    std::vector<uint8_t> data((size_t)w * h * 4);
    std::vector<png_bytep> rows(h);
    for (int y = 0; y < h; y++) rows[y] = data.data() + (size_t)y * w * 4;
    png_read_image(g.p, rows.data());
    png_read_end(g.p, nullptr);
    return data;
}
} // namespace

bool pngSize(const std::string& path, int& w, int& h) {
    Png g;
    if (!openPng(path, g, w, h)) return false;
    return w > 0 && h > 0;
}

Bitmap loadMask(const std::string& path) {
    Png g;
    int w = 0, h = 0;
    Bitmap b;
    if (!openPng(path, g, w, h)) {
        fprintf(stderr, "warning: cannot load %s\n", path.c_str());
        return b;
    }
    auto data = readRgba(g, w, h);
    b.reset(w, h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const uint8_t* p = &data[((size_t)y * w + x) * 4];
            int luma = (p[0] * 30 + p[1] * 59 + p[2] * 11) / 100;
            b.px[(size_t)y * w + x] = (p[3] > 127 && luma < 128) ? 1 : 0;
        }
    }
    return b;
}

Sprite loadSprite(const std::string& path, int frameW, int frameH) {
    Png g;
    int w = 0, h = 0;
    Sprite s;
    if (!openPng(path, g, w, h)) {
        fprintf(stderr, "warning: cannot load %s\n", path.c_str());
        return s;
    }
    auto data = readRgba(g, w, h);
    s.frameW = frameW;
    s.frameH = frameH;
    s.frames = w / frameW;
    if (s.frames < 1) s.frames = 1;
    s.ink.assign((size_t)s.frames * frameW * frameH, 0);
    s.opaque.assign((size_t)s.frames * frameW * frameH, 0);
    for (int f = 0; f < s.frames; f++) {
        for (int y = 0; y < frameH && y < h; y++) {
            for (int x = 0; x < frameW; x++) {
                int sx = f * frameW + x;
                if (sx >= w) continue;
                const uint8_t* p = &data[((size_t)y * w + sx) * 4];
                int luma = (p[0] * 30 + p[1] * 59 + p[2] * 11) / 100;
                size_t o = ((size_t)f * frameH + y) * frameW + x;
                if (p[3] > 127) {
                    s.opaque[o] = 1;
                    if (luma < 128) s.ink[o] = 1;
                }
            }
        }
    }
    return s;
}

void Bitmap::blit(const Bitmap& src, int ox, int oy) {
    for (int y = 0; y < src.h; y++) {
        int dy = oy + y;
        if (dy < 0 || dy >= h) continue;
        for (int x = 0; x < src.w; x++) {
            int dx = ox + x;
            if (dx < 0 || dx >= w) continue;
            if (src.at(x, y)) at(dx, dy) = 1;
        }
    }
}

void Bitmap::blitSprite(const Sprite& s, int frame, int ox, int oy) {
    const uint8_t* px = s.frame(frame);
    const uint8_t* a = s.alpha(frame);
    if (!px || !a) return;
    for (int y = 0; y < s.frameH; y++) {
        int dy = oy + y;
        if (dy < 0 || dy >= h) continue;
        for (int x = 0; x < s.frameW; x++) {
            int dx = ox + x;
            if (dx < 0 || dx >= w) continue;
            size_t i = (size_t)y * s.frameW + x;
            if (a[i]) at(dx, dy) = px[i];
        }
    }
}

bool writePng(const std::string& path, int w, int h, const uint8_t* rgb) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    png_structp p = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!p) { fclose(f); return false; }
    png_infop info = png_create_info_struct(p);
    if (!info) { png_destroy_write_struct(&p, nullptr); fclose(f); return false; }
    if (setjmp(png_jmpbuf(p))) { png_destroy_write_struct(&p, &info); fclose(f); return false; }
    png_init_io(p, f);
    png_set_IHDR(p, info, w, h, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(p, info);
    std::vector<png_bytep> rows(h);
    for (int y = 0; y < h; y++) rows[y] = const_cast<png_bytep>(rgb + (size_t)y * w * 3);
    png_write_image(p, rows.data());
    png_write_end(p, nullptr);
    png_destroy_write_struct(&p, &info);
    fclose(f);
    return true;
}
