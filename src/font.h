#pragma once
#include "image.h"
#include <cstdint>
#include <string>
#include <vector>

// Classic Macintosh NFNT bitmap font.
// Layout recovered from the original resources:
//   header (26 bytes) | bit image (rowWords*2 * fRectHeight) | offset table (2 bytes/char)
// The offset table holds cumulative glyph positions in pixels along the strip, so
// glyph(c) occupies columns [pos[c-first], pos[c-first+1]).
struct BitmapFont {
    int first = 0, last = 0;
    int height = 0, ascent = 0, descent = 0, rowWords = 0;
    std::vector<uint8_t> bits; // height rows of rowWords*2 bytes
    std::vector<uint16_t> pos;

    bool load(const std::string& path);
    bool valid() const { return height > 0 && !pos.empty(); }
    int advance(int c) const;
    // Draw at top-left (x,y); scale is an integer pixel magnification.
    void draw(Bitmap& out, const std::string& utf8, int x, int y, int scale = 1) const;
    int width(const std::string& utf8) const;
};
