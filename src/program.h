#pragma once
#include <string>
#include <vector>

enum class Curve { Linear, EaseIn, EaseOut, EaseInOut, EaseOutBack };
float applyCurve(Curve c, float t);
Curve parseCurve(const std::string& s);

enum class Op {
    Let, Camera, Scroll, MouseMove, MouseDown, MouseUp, MouseClick, Keyboard,
    Add, Scale, Zoom, Block, Label, Gosub, Goto, Return, Exit,
    Wait, WaitFor, WaitUntil, Cancel, ResetControls
};

struct ControlDecl {
    enum Type {
        Icon, FinderIcon, Cursor, Caret, Text, Time, Date, Slider, Button,
        RadioButton, RadioGroup, Menu, Canvas, Puzzle, StartupIcon, ImageButton, CursorRect
    } type = Icon;
    float x = 0, y = 0, w = 0, h = 0, value = 0;
    bool hasValue = false;
    std::string name, action;
    double startTime = 0;

    // Runtime state. The original classes keep this per control; the scene
    // compositor draws it and the input router (Processor) mutates it.
    int group = -1;            // radiobuttongroup id (radios only)
    bool selected = false, highlighted = false, tracking = false;
    bool mouseDown = false;    // menu open flag
    bool happy = false;        // startup icon shows the Happy Mac
    bool trashed = false;      // finder icon dropped on the trash
    int hilitedRow = -1;       // menu row
    double flashStart = -1;    // menu click flash
    double offset = 24;        // slider knob offset, 0..84
    double dragStart = 0;      // slider drag anchor
    float dragX = 0, dragY = 0;// finder icon drag delta
    float pressX = 0, pressY = 0; // pointerDown location
    std::string text = "0";    // text control contents
    // Text control: flags = the `value` operand (1 = blinking caret,
    // 2 = right-aligned field, 4 = calculator field); selection is in glyph
    // indices, end exclusive.
    int selStart = 0, selEnd = 0;
    bool textTracking = false;
    double textFirstDraw = -1;  // caret blink phase starts at the first draw
    int tiles[16] = {1, 2, 3, 13, 11, 0, 4, 6, 8, 7, 12, 14, 9, 15, 5, 10};
    // Canvas pen path, in scene coordinates (x0,y0,x1,y1 per segment).
    std::vector<float> strokes;
    float penX = -1, penY = -1;
};

// Top-down control rectangle in scene units. The `at` y of a control is the
// bottom edge of its frame (the original stores frames in CG bottom-up space);
// the menu and the startup icon are specified at their top edge instead.
struct Box {
    int x = 0, y = 0, w = 0, h = 0;
};
Box controlBox(const ControlDecl& c);

struct Instr {
    Op op = Op::Exit;
    std::string s0, s1;      // text operands (asset name, label, async id)
    float x = 0, y = 0, dur = 0;
    bool hasDur = false, async = false;
    Curve curve = Curve::Linear;
    ControlDecl ctl;
    std::vector<Instr> body;  // simultaneous block

    // runtime (single instance per program copy)
    double endTime = 0;
};

struct Program {
    std::string name;
    std::vector<Instr> code;
    std::vector<std::string> ignored; // lines no instruction claimed
    bool parse(const std::string& text);
};

// Executes one program, mirroring WallpaperMacintoshCore.Processor.
class Processor {
public:
    void setProgram(const Program* p);
    void reset(double now);

    // advances the VM; returns false once the program has exited
    bool tick(double now);
    void updateAnimations(double now);

    // Pointer router. Coordinates are scene units, the same space as `add` and
    // the scripted `mouse` instructions. down/up also fire control actions.
    void pointerDown(float x, float y);
    void pointerMove(float x, float y);
    void pointerUp(float x, float y);
    void pointerClick(float x, float y);

    // state consumed by the renderer
    float cameraX = 0, cameraY = 0;
    float scrollX = 256, scrollY = 171;
    float scale = 1.0f, zoom = 0.0f; // zoom 0 = normal, 1 = "in" (x3)
    float mouseX = 0, mouseY = 0;
    bool mouseDownState = false;
    std::string imageName, sceneName;
    std::string cursorName = "Arrow";
    std::string rampName = "None";
    bool paused = false;
    bool finished = false;
    std::vector<ControlDecl> controls;
    double clock = 0;
    double mediaTime = 0; // accumulated playback time, drives the spectrum colour
    // Actions fired by scripted clicks (nearest control with an action), drained by the host.
    std::vector<std::string> firedActions;

private:
    struct Anim {
        enum Kind { Cam, Scr, Scl, Zm, Ms } kind = Cam;
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        double t0 = 0, dur = 0;
        Curve curve = Curve::Linear;
        std::string id;
        bool done = false;
    };
    struct Frame {
        size_t ip = 0;
        size_t end = 0;
    };

    double startInstruction(const Instr& in, double now);
    void jumpTo(const std::string& label);
    void addControl(ControlDecl c, double now);

    const Program* prog_ = nullptr;
    std::vector<size_t> labels_;
    std::vector<Frame> stack_;
    size_t ip_ = 0;
    size_t blockEnd_ = 0;
    double blockedUntil_ = 0;
    double lastTick_ = 0;
    std::vector<Anim> anims_;
    int pressed_ = -1; // index of the control that received pointerDown
    int radioGroup_ = 0;
    float prevMouseX_ = 0, prevMouseY_ = 0;
    bool prevDown_ = false;

    friend struct ProcessorTest;
};
