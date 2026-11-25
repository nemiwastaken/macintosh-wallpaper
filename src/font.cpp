#include "font.h"
#include <cstdio>
#include <cstring>

namespace {
inline int be16(const uint8_t* p) { return (p[0] << 8) | p[1]; }
}

bool BitmapFont::load(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 32) { fclose(f); return false; }
    std::vector<uint8_t> d((size_t)n);
    if (fread(d.data(), 1, d.size(), f) != d.size()) { fclose(f); return false; }
    fclose(f);

    if (be16(&d[0]) != 0x9000 && be16(&d[0]) != 0x9001) return false;
    first = be16(&d[2]);
    last = be16(&d[4]);
    height = be16(&d[14]);   // fRectHeight
    ascent = be16(&d[18]);
    descent = be16(&d[20]);
    rowWords = be16(&d[24]);
    if (last < first || height <= 0 || rowWords <= 0) return false;

    size_t rowBytes = (size_t)rowWords * 2;
    size_t imgBytes = rowBytes * (size_t)height;
    size_t table = 26 + imgBytes;
    size_t chars = (size_t)(last - first + 1);
    if (table + (chars + 1) * 2 > d.size()) return false;

    bits.assign(d.begin() + 26, d.begin() + 26 + (long)imgBytes);
    pos.resize(chars + 1);
    for (size_t i = 0; i <= chars; i++)
        pos[i] = (uint16_t)be16(&d[table + i * 2]);
    return true;
}

int BitmapFont::advance(int c) const {
    if (c < first || c > last) return 0;
    size_t i = (size_t)(c - first);
    if (i + 1 >= pos.size()) return 0;
    int a = (int)pos[i + 1] - (int)pos[i];
    if (a == 0 && c == 32) a = 4; // the resource has no ink for the space
    return a;
}

int BitmapFont::width(const std::string& utf8) const {
    int w = 0;
    for (unsigned char c : utf8)
        if (c >= 32) w += advance(c);
    return w;
}

void BitmapFont::draw(Bitmap& out, const std::string& utf8, int x, int y, int scale) const {
    size_t rowBytes = (size_t)rowWords * 2;
    int penX = x;
    for (unsigned char c : utf8) {
        if (c < 32) continue;
        int adv = advance(c);
        if (c >= first && c <= last) {
            int gx0 = pos[(size_t)(c - first)];
            int gx1 = pos[(size_t)(c - first) + 1];
            for (int row = 0; row < height; row++) {
                const uint8_t* rp = bits.data() + (size_t)row * rowBytes;
                for (int gx = gx0; gx < gx1 && gx < (int)rowBytes * 8; gx++) {
                    int byte = gx >> 3, bit = 7 - (gx & 7);
                    if (!(rp[byte] & (1 << bit))) continue;
                    int px = penX + (gx - gx0);
                    int py = y + row;
                    for (int sy = 0; sy < scale; sy++)
                        for (int sx = 0; sx < scale; sx++) {
                            int dx = px + sx, dy = py + sy;
                            if (dx >= 0 && dx < out.w && dy >= 0 && dy < out.h)
                                out.at(dx, dy) = 1;
                        }
                }
            }
        }
        penX += adv;
    }
}
