#pragma once
#include "image.h"
#include "font.h"
#include "program.h"
#include <map>
#include <string>

// A classic 16x16 CURS resource: icon bits + mask + hotspot.
struct CursorBmp {
    Bitmap icon, mask;
    int hx = 0, hy = 0;
    bool valid() const { return icon.w > 0; }
};

// AnimatedCursor: a list of CURS frames (acur_*.bin) advanced every `ticks`
// 60 Hz ticks. The watch, hand, ball and Clarus cursors are animated.
struct AnimatedCursor {
    std::vector<CursorBmp> frames;
    std::vector<int> ids; // the CURS resource ids, in playback order
    int ticks = 8;
    const CursorBmp* frame(double elapsed) const;
};

// Composes the 512x342 1-bit "screen contents" exactly like the original
// CGBitmapContext: base PNG first, then animated controls.
class Scene {
public:
    bool init(const std::string& resourcesDir);
    void compose(const std::string& imageName, std::vector<ControlDecl>& controls,
                 const std::string& cursorName, double now, bool use24h,
                 double caretInterval = 0.5333, float mouseX = 0, float mouseY = 0,
                 bool mouseDown = false);
    void resetCanvas();

    // True when `name` is a cursor this scene can draw (photo, pointer, brush).
    bool hasCursor(const std::string& name);
    // True when <name>.png loads as a strip of frameW x frameH frames.
    bool hasSprite(const std::string& name, int frameW, int frameH);
    // The loaded strip, for the built-in placement audit.
    const Sprite* getSprite(const std::string& name, int frameW, int frameH);
    // Frame resource ids of an AnimatedCursor, for the built-in audit.
    const std::vector<int>* animatedIds(const std::string& name);

    const Bitmap& mask() const { return out_; }
    int width() const { return out_.w; }
    int height() const { return out_.h; }

private:
    Bitmap base(const std::string& name);
    Sprite* sprite(const std::string& name, int frameW, int frameH);
    CursorBmp* cursor(const std::string& name);
    AnimatedCursor* animatedCursor(const std::string& name);
    BitmapFont* font(const std::string& name);
    void drawCursor(Bitmap& frame, const std::string& name, float x, float y, double now,
                    double startTime = 0);
    void drawWidget(Bitmap& frame, const ControlDecl& c, double now);
    void penStroke(float x0, float y0, float x1, float y1);

    std::string dir_;
    Bitmap out_;
    std::map<std::string, Bitmap> bases_;
    std::map<std::string, Sprite> sprites_;
    std::map<std::string, CursorBmp> cursors_;
    std::map<std::string, AnimatedCursor> animated_;
    std::map<std::string, BitmapFont> fonts_;
    std::string image_;

    // Desk Accessories' drawing canvas (persistent across frames).
    Bitmap canvas_;
    size_t drawnStrokes_ = 0;
    bool prevMouseDown_ = false;
};
