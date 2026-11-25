#include "scene.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

// Decode a classic 16x16 CURS resource: 32 bytes bitmap + 32 bytes mask + hotspot.
static CursorBmp decodeCursor(const std::string& path) {
    CursorBmp c;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return c;
    uint8_t d[68] = {0};
    size_t n = fread(d, 1, sizeof d, f);
    fclose(f);
    if (n < 64) return c;
    c.icon.reset(16, 16);
    c.mask.reset(16, 16);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            int i = y * 16 + x;
            c.icon.px[i] = (d[i / 8] >> (7 - (i % 8))) & 1;
            c.mask.px[i] = (d[32 + i / 8] >> (7 - (i % 8))) & 1;
        }
    if (n >= 68) {
        c.hx = (d[64] << 8) | d[65];
        c.hy = (d[66] << 8) | d[67];
    }
    return c;
}

bool Scene::init(const std::string& resourcesDir) {
    dir_ = resourcesDir;
    if (!dir_.empty() && dir_.back() != '/') dir_.push_back('/');
    return true;
}

void Scene::resetCanvas() {
    canvas_ = Bitmap();
    drawnStrokes_ = 0;
}

Bitmap Scene::base(const std::string& name) {
    auto it = bases_.find(name);
    if (it != bases_.end()) return it->second;
    Bitmap b;
    if (name.empty()) return b; // a program without `let image` keeps a blank screen
    b = loadMask(dir_ + name + ".png");
    bases_[name] = b;
    return b;
}

Sprite* Scene::sprite(const std::string& name, int frameW, int frameH) {
    std::string key = name + "@" + std::to_string(frameW) + "x" + std::to_string(frameH);
    auto it = sprites_.find(key);
    if (it != sprites_.end()) return &it->second;
    Sprite s = loadSprite(dir_ + name + ".png", frameW, frameH);
    auto r = sprites_.emplace(key, std::move(s));
    return &r.first->second;
}

CursorBmp* Scene::cursor(const std::string& name) {
    if (name.empty() || name == "None") return nullptr;
    static const std::map<std::string, std::string> ids = {
        {"Automatic", "0"}, {"Arrow", "0"}, {"Text", "1"}, {"Crosshair", "2"}};
    auto it = ids.find(name);
    if (it == ids.end()) return nullptr;
    auto c = cursors_.find(it->second);
    if (c != cursors_.end()) return c->second.valid() ? &c->second : nullptr;
    CursorBmp b = decodeCursor(dir_ + "CURS_" + it->second + ".bin");
    auto r = cursors_.emplace(it->second, std::move(b));
    return r.first->second.valid() ? &r.first->second : nullptr;
}

// Watch/hand/ball/Clarus are AnimatedCursor resources: an acur_*.bin file
// lists the CURS resource ids of the frames (the original loads the list as
// Data and constructs one Cursor per id).
AnimatedCursor* Scene::animatedCursor(const std::string& name) {
    static const std::map<std::string, std::string> files = {
        {"Watch", "acur_2400"}, {"Hand", "acur_-20714"},
        {"Ball", "acur_128"},   {"Clarus", "acur_640"}};
    auto it = files.find(name);
    if (it == files.end()) return nullptr;
    auto c = animated_.find(name);
    if (c != animated_.end()) return c->second.frames.empty() ? nullptr : &c->second;

    AnimatedCursor a;
    FILE* f = fopen((dir_ + it->second + ".bin").c_str(), "rb");
    if (f) {
        unsigned char hdr[4];
        if (fread(hdr, 1, 4, f) == 4) {
            // uint16 BE frame count, then one 16-bit BE resource id per 4-byte
            // slot; the ids are signed (the hand frames are -20714 … -20709).
            unsigned n = (unsigned)hdr[0] << 8 | hdr[1];
            for (unsigned i = 0; i < n; i++) {
                unsigned char b[4];
                if (fread(b, 1, 4, f) != 4) break;
                int id = (int16_t)((unsigned)b[0] << 8 | b[1]);
                a.ids.push_back(id);
                CursorBmp bmp = decodeCursor(dir_ + "CURS_" + std::to_string(id) + ".bin");
                if (bmp.valid()) a.frames.push_back(std::move(bmp));
            }
        }
        fclose(f);
    }
    auto r = animated_.emplace(name, std::move(a));
    return r.first->second.frames.empty() ? nullptr : &r.first->second;
}

const CursorBmp* AnimatedCursor::frame(double elapsed) const {
    if (frames.empty()) return nullptr;
    int64_t t = (int64_t)(std::max(0.0, elapsed) * 60.0);
    size_t i = (size_t)(t / std::max(1, ticks)) % frames.size();
    return &frames[i];
}

bool Scene::hasCursor(const std::string& name) {
    if (name.empty() || name == "None" || name == "Automatic" || name == "Brush")
        return true;
    return animatedCursor(name) || cursor(name);
}

bool Scene::hasSprite(const std::string& name, int frameW, int frameH) {
    if (name.empty()) return false;
    Sprite* s = sprite(name, frameW, frameH);
    return s && s->frames > 0 && s->frameW == frameW && s->frameH == frameH;
}

const Sprite* Scene::getSprite(const std::string& name, int frameW, int frameH) {
    return sprite(name, frameW, frameH);
}

const std::vector<int>* Scene::animatedIds(const std::string& name) {
    AnimatedCursor* a = animatedCursor(name);
    return a && !a->ids.empty() ? &a->ids : nullptr;
}

BitmapFont* Scene::font(const std::string& name) {
    std::string file = name.empty() ? "Chicago12" : name;
    auto it = fonts_.find(file);
    if (it != fonts_.end()) return it->second.valid() ? &it->second : nullptr;
    BitmapFont f;
    f.load(dir_ + file + ".bin");
    auto r = fonts_.emplace(file, std::move(f));
    return r.first->second.valid() ? &r.first->second : nullptr;
}

// Recovered from the TimeText initializer: the formatter's dateFormat is
// "hh:mm:ss" or "HH:mm:ss" depending on Locale.current.hourCycle.
static std::string fmtTime(bool use24h) {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[64];
    strftime(buf, sizeof buf, use24h ? "%H:%M:%S" : "%I:%M:%S", &tm);
    return buf;
}

// Recovered from the DateText initializer: dateFormat is "MM/dd/yy" regardless of
// isSystem1 (the flag only selects the drawing style).
static std::string fmtDate() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[96];
    strftime(buf, sizeof buf, "%m/%d/%y", &tm);
    return buf;
}

// Cursor composition recovered from sub_10001BD8C: mask=1 → the icon bit picks
// black or white; mask=0 with the icon bit set → invert the destination.
static void blitCursor(Bitmap& dst, const CursorBmp& c, int ox, int oy) {
    for (int y = 0; y < 16; y++) {
        int dy = oy + y;
        if (dy < 0 || dy >= dst.h) continue;
        for (int x = 0; x < 16; x++) {
            int dx = ox + x;
            if (dx < 0 || dx >= dst.w) continue;
            int i = y * 16 + x;
            if (c.mask.px[i]) dst.at(dx, dy) = c.icon.px[i] ? 1 : 0;
            else if (c.icon.px[i]) dst.at(dx, dy) ^= 1;
        }
    }
}

void Scene::drawCursor(Bitmap& frame, const std::string& name, float x, float y, double now,
                       double startTime) {
    if (name == "Brush") { // BrushCursor: 4x4 inverted dot at the pointer
        int cx = (int)x, cy = (int)y;
        for (int dy = -2; dy < 2; dy++)
            for (int dx = -2; dx < 2; dx++) {
                float fx = dx + 0.5f, fy = dy + 0.5f;
                if (fx * fx + fy * fy > 4.0f) continue;
                int px = cx + dx, py = cy + dy;
                if (px >= 0 && px < frame.w && py >= 0 && py < frame.h) frame.at(px, py) ^= 1;
            }
        return;
    }
    if (AnimatedCursor* a = animatedCursor(name)) {
        if (const CursorBmp* c = a->frame(now - startTime))
            blitCursor(frame, *c, (int)x - c->hx, (int)y - c->hy);
        return;
    }
    CursorBmp* c = cursor(name);
    if (!c) return;
    blitCursor(frame, *c, (int)x - c->hx, (int)y - c->hy);
}

void Scene::penStroke(float x0, float y0, float x1, float y1) {
    if (canvas_.w == 0) return;
    float dx = x1 - x0, dy = y1 - y0;
    float len = std::max(std::fabs(dx), std::fabs(dy));
    int steps = std::max(1, (int)std::ceil(len));
    for (int s = 0; s <= steps; s++) {
        float t = (float)s / steps;
        int cx = (int)std::lround(x0 + dx * t);
        int cy = (int)std::lround(y0 + dy * t);
        // 4 px round pen, as in the recovered Canvas setup.
        for (int py = -2; py < 2; py++)
            for (int px = -2; px < 2; px++) {
                if (px * px + py * py > 4) continue;
                int x = cx + px, y = cy + py;
                if (x >= 0 && x < canvas_.w && y >= 0 && y < canvas_.h) canvas_.at(x, y) = 1;
            }
    }
}

// Draws the interactive widgets. The resting artwork lives in the base PNG, so
// every widget that can change state paints its whole frame (paper included).
void Scene::drawWidget(Bitmap& frame, const ControlDecl& c, double now) {
    switch (c.type) {
        case ControlDecl::Slider: {
            Sprite* s = sprite("Slider", 23, 11);
            if (!s || !s->frames) break;
            int top = (int)std::lround(c.y) - (int)std::lround(c.offset) - 10;
            frame.blitSprite(*s, 0, (int)c.x, top);
            break;
        }
        case ControlDecl::RadioButton: {
            if (c.name.empty() || c.name == "None") {
                // No image: the class inverts the frame to show selection
                // (Alert Sounds rows, MacPaintHello's tool boxes).
                if (!c.selected) break;
                Box b = controlBox(c);
                for (int y = b.y; y < b.y + b.h; y++)
                    for (int x = b.x; x < b.x + b.w; x++)
                        if (x >= 0 && x < frame.w && y >= 0 && y < frame.h) frame.at(x, y) ^= 1;
                break;
            }
            Sprite* s = sprite(c.name, 16, 16);
            if (!s || !s->frames) break;
            int idx = (c.selected ? 2 : 0) + (c.highlighted ? 1 : 0);
            frame.blitSprite(*s, idx, (int)c.x, (int)c.y - 15);
            break;
        }
        case ControlDecl::ImageButton: {
            int w = c.w > 0 ? (int)c.w : 74, h = c.h > 0 ? (int)c.h : 18;
            Sprite* s = sprite(c.name.empty() ? "CancelButton" : c.name, w, h);
            if (!s || !s->frames) break;
            frame.blitSprite(*s, c.highlighted ? 1 : 0, (int)c.x, (int)c.y - h + 1);
            break;
        }
        case ControlDecl::Button: {
            if (!c.highlighted) break;
            Box b = controlBox(c);
            for (int y = b.y; y < b.y + b.h; y++)
                for (int x = b.x; x < b.x + b.w; x++)
                    if (x >= 0 && x < frame.w && y >= 0 && y < frame.h) frame.at(x, y) ^= 1;
            break;
        }
        case ControlDecl::Puzzle: {
            Sprite* s = sprite("PuzzleTiles", 19, 19);
            if (!s || !s->frames) break;
            for (int i = 0; i < 16; i++) {
                int col = i & 3, row = i >> 2;
                frame.blitSprite(*s, c.tiles[i], (int)c.x + 20 * col,
                                 (int)c.y - 79 + 20 * row);
            }
            break;
        }
        case ControlDecl::Menu: {
            if (!c.mouseDown) break;
            Sprite* s = sprite("MacWriteStyleMenu", 119, 213);
            if (!s || !s->frames) break;
            frame.blitSprite(*s, 0, (int)c.x, (int)c.y + 1);
            bool visible = true;
            if (c.flashStart >= 0) // closing blink: 0.1 s off / 0.1 s on
                visible = std::fmod(now - c.flashStart, 0.2) >= 0.1;
            if (visible && c.hilitedRow >= 0) {
                int top = 324 - 16 * c.hilitedRow;
                for (int y = top; y < top + 16; y++)
                    for (int x = (int)c.x + 1; x < (int)c.x + 120; x++)
                        if (x >= 0 && x < frame.w && y >= 0 && y < frame.h) frame.at(x, y) ^= 1;
            }
            break;
        }
        case ControlDecl::StartupIcon: {
            Sprite* s = sprite(c.name.empty() ? "StartupIcons" : c.name, 32, 32);
            if (!s || !s->frames) break;
            int idx = c.happy ? 2 : (std::fmod(now - c.startTime, 1.5) > 0.75 ? 1 : 0);
            frame.blitSprite(*s, idx, (int)c.x, (int)c.y);
            break;
        }
        case ControlDecl::FinderIcon: {
            Sprite* s = sprite(c.name, 64, 48);
            if (!s || !s->frames) break;
            int idx = c.trashed ? 2 : (c.tracking ? 3 : (c.selected ? 1 : 0));
            frame.blitSprite(*s, idx, (int)(c.x + c.dragX),
                             (int)c.y - 47 + (int)c.dragY);
            break;
        }
        default:
            break;
    }
}

void Scene::compose(const std::string& imageName, std::vector<ControlDecl>& controls,
                    const std::string& cursorName, double now, bool use24h,
                    double caretInterval, float mouseX, float mouseY, bool mouseDown) {
    if (imageName != image_) {
        image_ = imageName;
        resetCanvas();
        out_ = base(imageName.empty() ? "IconGarden" : imageName);
        if (out_.w == 0) out_.reset(512, 342);
    }
    if (out_.w == 0) { out_.reset(512, 342); image_.clear(); }

    // Start each frame from the base scene image.
    Bitmap frame = base(image_);
    if (frame.w == 0) frame.reset(512, 342);

    // The drawing canvas is cleared and resized to the control's frame.
    const ControlDecl* canvas = nullptr;
    for (const auto& c : controls)
        if (c.type == ControlDecl::Canvas) canvas = &c;
    if (canvas) {
        int cw = canvas->w > 0 ? (int)canvas->w : 140;
        int ch = canvas->h > 0 ? (int)canvas->h : 50;
        if (canvas_.w != cw || canvas_.h != ch) {
            canvas_.reset(cw, ch);
            drawnStrokes_ = 0;
        }
        // Replay any pen segments recorded since the last frame (the VM runs
        // ahead during --at fast-forward, so strokes are stored, not painted).
        if (canvas->strokes.size() / 4 < drawnStrokes_) {
            canvas_.reset(cw, ch);
            drawnStrokes_ = 0;
        }
        for (size_t i = drawnStrokes_; i < canvas->strokes.size() / 4; i++) {
            const float* s = &canvas->strokes[i * 4];
            penStroke(s[0] - canvas->x, s[1] - (canvas->y - ch + 1),
                      s[2] - canvas->x, s[3] - (canvas->y - ch + 1));
        }
        drawnStrokes_ = canvas->strokes.size() / 4;
    }

    for (auto& c : controls) {
        Box b = controlBox(c);
        switch (c.type) {
            case ControlDecl::Icon: {
                Sprite* s = sprite(c.name, 32, 32);
                if (!s || !s->frames) break;
                int ticks = std::max(1, (int)std::lround(c.value));
                int f = (int)(((now - c.startTime) * 60.0) / ticks);
                const uint8_t* px = s->frame(f);
                int top = (int)c.y - 31; // widget frames are bottom-anchored
                for (int y = 0; y < s->frameH; y++)
                    for (int x = 0; x < s->frameW; x++)
                        if (px[y * s->frameW + x]) {
                            int dx = (int)c.x + x, dy = top + y;
                            if (dx >= 0 && dx < frame.w && dy >= 0 && dy < frame.h)
                                frame.at(dx, dy) = 1;
                        }
                break;
            }
            case ControlDecl::FinderIcon:
            case ControlDecl::Slider:
            case ControlDecl::RadioButton:
            case ControlDecl::ImageButton:
            case ControlDecl::Button:
            case ControlDecl::Puzzle:
            case ControlDecl::Menu:
            case ControlDecl::StartupIcon:
                drawWidget(frame, c, now);
                break;
            case ControlDecl::Cursor: {
                const CursorBmp* cb = nullptr;
                if (AnimatedCursor* a = animatedCursor(c.name))
                    cb = a->frame(now - c.startTime);
                else
                    cb = cursor(c.name);
                if (!cb) break;
                blitCursor(frame, *cb, (int)c.x - cb->hx, (int)c.y - 15 - cb->hy);
                break;
            }
            case ControlDecl::Caret: {
                double iv = caretInterval > 0.01 ? caretInterval : 0.5333;
                bool on = std::fmod(now - c.startTime, iv * 2.0) < iv;
                if (!on) break;
                int w = std::max(1, (int)c.w), h = std::max(1, (int)c.h);
                int top = (int)c.y - h + 1;
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++) {
                        int dx = (int)c.x + x, dy = top + y;
                        if (dx >= 0 && dx < frame.w && dy >= 0 && dy < frame.h)
                            frame.at(dx, dy) = 1;
                    }
                break;
            }
            case ControlDecl::Time: {
                BitmapFont* f = font(c.name);
                if (!f) break;
                f->draw(frame, fmtTime(use24h), (int)c.x, (int)c.y);
                break;
            }
            case ControlDecl::Date: {
                BitmapFont* f = font(c.name);
                if (!f) break;
                f->draw(frame, fmtDate(), (int)c.x, (int)c.y);
                break;
            }
            case ControlDecl::Text: {
                BitmapFont* f = font(c.name);
                if (!f || c.text.empty()) break;
                // Text.Flags is the `value` operand recovered from sub_1000198D4:
                // bit 0 = blinking caret, bit 1 = right-aligned field.
                bool right = c.hasValue && ((int)c.value & 2);
                int lineH = f->height + 2;
                struct Glyph { int x, y, w, h; };
                std::vector<Glyph> rects;
                std::vector<std::string> lines;
                std::vector<int> lineX;
                size_t start = 0;
                while (start <= c.text.size()) {
                    size_t nl = c.text.find('\n', start);
                    lines.push_back(c.text.substr(
                        start, nl == std::string::npos ? std::string::npos : nl - start));
                    if (nl == std::string::npos) break;
                    start = nl + 1;
                }
                for (size_t ln = 0; ln < lines.size(); ln++) {
                    int x = (int)c.x;
                    if (right) x -= f->width(lines[ln]);
                    lineX.push_back(x);
                    int pen = x;
                    for (unsigned char ch : lines[ln]) {
                        int adv = f->advance(ch);
                        if (ch >= 32) rects.push_back({pen, (int)c.y + (int)ln * lineH, adv, f->height});
                        pen += adv;
                    }
                }
                // Original Text mouse handling (down 0x100019FD8, drag 0x10001A0B4):
                // the glyph under the pointer is found, and the insertion point is
                // its left or right half; the same rule runs on drag.
                auto hit = [&](float px, float py) -> int {
                    for (size_t i = 0; i < rects.size(); i++) {
                        const Glyph& g = rects[i];
                        if (px >= g.x && px < g.x + g.w && py >= g.y && py < g.y + g.h)
                            return (int)i + (px > g.x + g.w * 0.5f ? 1 : 0);
                    }
                    return -1;
                };
                if (mouseDown && !prevMouseDown_) {
                    int i = hit(mouseX, mouseY);
                    if (i >= 0) {
                        c.textTracking = true;
                        c.selStart = c.selEnd = i;
                    }
                } else if (!mouseDown) {
                    c.textTracking = false;
                } else if (c.textTracking) {
                    int i = hit(mouseX, mouseY);
                    if (i >= 0) c.selEnd = i;
                }
                for (size_t ln = 0; ln < lines.size(); ln++)
                    f->draw(frame, lines[ln], lineX[ln], (int)c.y + (int)ln * lineH);
                int a = std::min(c.selStart, c.selEnd);
                int b = std::max(c.selStart, c.selEnd);
                a = std::max(0, std::min(a, (int)rects.size()));
                b = std::max(0, std::min(b, (int)rects.size()));
                for (int i = a; i < b; i++) {
                    const Glyph& g = rects[i];
                    for (int yy = g.y; yy < g.y + g.h; yy++)
                        for (int xx = g.x; xx < g.x + g.w; xx++)
                            if (xx >= 0 && xx < frame.w && yy >= 0 && yy < frame.h)
                                frame.at(xx, yy) ^= 1;
                }
                if ((c.hasValue && ((int)c.value & 1)) && a == b) {
                    if (c.textFirstDraw < 0) c.textFirstDraw = now;
                    double iv = caretInterval > 0.01 ? caretInterval : 0.5333;
                    if (std::fmod(now - c.textFirstDraw, iv + iv) < iv) {
                        int i = a;
                        int cx = (int)c.x, cy = (int)c.y;
                        if (i < (int)rects.size()) {
                            cx = rects[i].x;
                            cy = rects[i].y;
                        } else if (!rects.empty()) {
                            cx = rects.back().x + rects.back().w;
                            cy = rects.back().y;
                        }
                        for (int yy = cy; yy < cy + f->height - 1; yy++)
                            if (cx >= 0 && cx < frame.w && yy >= 0 && yy < frame.h)
                                frame.at(cx, yy) = 1;
                    }
                }
                break;
            }
            case ControlDecl::Canvas: {
                if (!canvas_.w) break;
                frame.blit(canvas_, b.x, b.y);
                break;
            }
            default:
                break;
        }
    }

    // Freehand drawing is replayed from the recorded pen path (above), so the
    // canvas already contains it before the control is composited.

    // Cursor: a cursorrect under the pointer overrides the scene cursor. The
    // pointer itself is drawn in scene space (the original composites it into
    // the same bitmap), so the scripted demos show a visible mouse.
    std::string eff = cursorName;
    for (const auto& c : controls) {
        if (c.type != ControlDecl::CursorRect) continue;
        Box b = controlBox(c);
        if (mouseX >= b.x && mouseX < b.x + b.w && mouseY >= b.y && mouseY < b.y + b.h)
            eff = c.name;
    }
    if (eff != "None" && !eff.empty()) drawCursor(frame, eff, mouseX, mouseY, now);

    prevMouseDown_ = mouseDown;
    out_ = std::move(frame);
}
