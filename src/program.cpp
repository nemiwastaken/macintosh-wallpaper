#include "program.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <regex>
#include <sstream>

Curve parseCurve(const std::string& s) {
    if (s == "easein") return Curve::EaseIn;
    if (s == "easeout") return Curve::EaseOut;
    if (s == "easeinout") return Curve::EaseInOut;
    if (s == "easeoutback") return Curve::EaseOutBack;
    return Curve::Linear;
}

float applyCurve(Curve c, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (c) {
        case Curve::EaseIn: return t * t;
        case Curve::EaseOut: return 1 - (1 - t) * (1 - t);
        case Curve::EaseInOut:
            return t < 0.5f ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t);
        case Curve::EaseOutBack: {
            const float s = 1.70158f;
            float u = t - 1;
            return 1 + (s + 1) * u * u * u + s * u * u;
        }
        case Curve::Linear: default: return t;
    }
}

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool getFloat(const std::string& s, const char* key, float& out) {
    std::regex re(std::string(key) + R"rx(\s+(-?[0-9.]+))rx");
    std::smatch m;
    if (std::regex_search(s, m, re)) { out = std::stof(m[1]); return true; }
    return false;
}

bool getPair(const std::string& s, const char* key, float& a, float& b) {
    std::regex re(std::string(key) + R"rx(\s+(-?[0-9.]+),\s*(-?[0-9.]+))rx");
    std::smatch m;
    if (std::regex_search(s, m, re)) { a = std::stof(m[1]); b = std::stof(m[2]); return true; }
    return false;
}

} // namespace

// ---------------------------------------------------------------- parsing

static bool parseMovement(const std::string& line, const std::string& verb, Instr& out) {
    // camera|scroll|mouse to X, Y [over N seconds] [curve C] [async "id"]
    std::regex re("^" + verb +
                  R"rx(\s+to\s+(-?[0-9.]+),\s*(-?[0-9.]+))rx"
                  R"rx((?:\s+over\s+([0-9.]+)\s+seconds?(?:\s+curve\s+(linear|easein|easeout|easeinout|easeoutback))?)?)rx"
                  R"rx((?:\s+async\s+"(\w+)")?\s*$)rx");
    std::smatch m;
    if (!std::regex_match(line, m, re)) return false;
    out.x = std::stof(m[1]);
    out.y = std::stof(m[2]);
    if (m[3].matched) { out.dur = std::stof(m[3]); out.hasDur = true; }
    if (m[4].matched) out.curve = parseCurve(m[4]);
    if (m[5].matched) { out.async = true; out.s0 = m[5]; }
    return true;
}

static bool parseScale(const std::string& line, Instr& out) {
    std::regex re(R"rx(^(scale|zoom)\s+(?:to\s+)?(in|out|-?[0-9.]+))rx"
                  R"rx((?:\s+over\s+([0-9.]+)\s+seconds?(?:\s+curve\s+(linear|easein|easeout|easeinout|easeoutback))?)?)rx"
                  R"rx((?:\s+async\s+"(\w+)")?\s*$)rx");
    std::smatch m;
    if (!std::regex_match(line, m, re)) return false;
    out.op = (m[1] == "zoom") ? Op::Zoom : Op::Scale;
    std::string v = m[2];
    out.x = (v == "in") ? 1.0f : (v == "out") ? 0.0f : std::stof(v);
    if (m[3].matched) { out.dur = std::stof(m[3]); out.hasDur = true; }
    if (m[4].matched) out.curve = parseCurve(m[4]);
    if (m[5].matched) { out.async = true; out.s0 = m[5]; }
    return true;
}

static bool parseAdd(const std::string& line, Instr& out) {
    std::regex re(R"rx(^add\s+(\w+)(.*)$)rx");
    std::smatch m;
    if (!std::regex_match(line, m, re)) return false;
    const std::string t = m[1];
    const std::string rest = m[2];
    out.op = Op::Add;
    auto& c = out.ctl;
    if (t == "icon") c.type = ControlDecl::Icon;
    else if (t == "findericon") c.type = ControlDecl::FinderIcon;
    else if (t == "cursor") c.type = ControlDecl::Cursor;
    else if (t == "caret") c.type = ControlDecl::Caret;
    else if (t == "text") c.type = ControlDecl::Text;
    else if (t == "time") c.type = ControlDecl::Time;
    else if (t == "date") c.type = ControlDecl::Date;
    else if (t == "slider") c.type = ControlDecl::Slider;
    else if (t == "button") c.type = ControlDecl::Button;
    else if (t == "radiobutton") c.type = ControlDecl::RadioButton;
    else if (t == "radiobuttongroup") c.type = ControlDecl::RadioGroup;
    else if (t == "menu") c.type = ControlDecl::Menu;
    else if (t == "canvas") c.type = ControlDecl::Canvas;
    else if (t == "puzzle") c.type = ControlDecl::Puzzle;
    else if (t == "startupicon") c.type = ControlDecl::StartupIcon;
    else if (t == "imagebutton") c.type = ControlDecl::ImageButton;
    else if (t == "cursorrect") c.type = ControlDecl::CursorRect;
    else return false;

    getPair(rest, "at", c.x, c.y);
    getPair(rest, "size", c.w, c.h);
    c.hasValue = getFloat(rest, "value", c.value);
    std::regex rn(R"rx(name\s+"([^"]*)")rx");
    std::smatch m2;
    if (std::regex_search(rest, m2, rn)) c.name = m2[1];
    std::regex ra(R"rx(action\s+"([^"]*)")rx");
    if (std::regex_search(rest, m2, ra)) c.action = m2[1];
    return true;
}

static bool parseLine(const std::string& raw, Instr& out) {
    std::string line = raw;
    std::smatch m;

    if (line.empty()) return false;

    {
        std::regex re(R"rx(^let\s+(\w+)\s*=\s*(?:"([^"]*)"|(\S+))\s*$)rx");
        if (std::regex_match(line, m, re)) {
            out.op = Op::Let;
            out.s0 = m[1];
            out.s1 = m[2].matched ? std::string(m[2]) : std::string(m[3]);
            return true;
        }
    }
    if (line == "start simultaneous") { out.op = Op::Block; return true; }
    if (line == "end simultaneous") return false; // handled by caller

    if (line == "return") { out.op = Op::Return; return true; }
    if (line == "exit") { out.op = Op::Exit; return true; }
    if (line == "reset controls") { out.op = Op::ResetControls; return true; }
    if (line == "mouse down") { out.op = Op::MouseDown; return true; }
    if (line == "mouse up") { out.op = Op::MouseUp; return true; }
    if (line == "mouse click") { out.op = Op::MouseClick; return true; }

    {
        std::regex re(R"rx(^gosub\s+(\w+)\s*$)rx");
        if (std::regex_match(line, m, re)) { out.op = Op::Gosub; out.s0 = m[1]; return true; }
    }
    {
        std::regex re(R"rx(^goto\s+(\w+)\s*$)rx");
        if (std::regex_match(line, m, re)) { out.op = Op::Goto; out.s0 = m[1]; return true; }
    }
    {
        std::regex re(R"rx(^(\w+):\s*$)rx");
        if (std::regex_match(line, m, re)) { out.op = Op::Label; out.s0 = m[1]; return true; }
    }
    {
        std::regex re(R"rx(^wait\s+for\s+([0-9.]+)\s+seconds?\s*$)rx");
        if (std::regex_match(line, m, re)) { out.op = Op::Wait; out.dur = std::stof(m[1]); return true; }
    }
    {
        std::regex re(R"rx(^wait\s+for\s+"(\w+)"\s+to\s+complete\s*$)rx");
        if (std::regex_match(line, m, re)) { out.op = Op::WaitFor; out.s0 = m[1]; return true; }
    }
    {
        std::regex re(R"rx(^wait\s+until\s+([0-9.]+)\s+seconds?\s+before\s+"(\w+)"\s+completes\s*$)rx");
        if (std::regex_match(line, m, re)) {
            out.op = Op::WaitUntil; out.dur = std::stof(m[1]); out.s0 = m[2]; return true;
        }
    }
    {
        std::regex re(R"rx(^cancel\s+"(\w+)"\s*$)rx");
        if (std::regex_match(line, m, re)) { out.op = Op::Cancel; out.s0 = m[1]; return true; }
    }
    {
        std::regex re(R"rx(^keyboard\s+"([^"]*)"\s*$)rx");
        if (std::regex_match(line, m, re)) { out.op = Op::Keyboard; out.s0 = m[1]; return true; }
    }

    {
        Instr t;
        if (parseMovement(line, "camera", t)) { t.op = Op::Camera; out = t; return true; }
        if (parseMovement(line, "scroll", t)) { t.op = Op::Scroll; out = t; return true; }
        if (parseMovement(line, "mouse", t)) { t.op = Op::MouseMove; out = t; return true; }
        if (parseScale(line, t)) { out = t; return true; }
        if (parseAdd(line, t)) { out = t; return true; }
    }
    return false;
}

bool Program::parse(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    std::vector<Instr> stack; // nested simultaneous blocks
    std::vector<Instr> top;

    auto target = [&]() -> std::vector<Instr>& {
        return stack.empty() ? top : stack.back().body;
    };

    while (std::getline(in, line)) {
        size_t c = line.find("//");
        if (c != std::string::npos) line = line.substr(0, c);
        line = trim(line);
        if (line.empty()) continue;

        if (line == "end simultaneous") {
            if (stack.empty()) continue;
            Instr blk = std::move(stack.back());
            stack.pop_back();
            target().push_back(std::move(blk));
            continue;
        }

        Instr ins;
        if (!parseLine(line, ins)) {
            ignored.push_back(line);
            fprintf(stderr, "program %s: ignored line: %s\n", name.c_str(), line.c_str());
            continue;
        }
        if (ins.op == Op::Block) {
            stack.push_back(std::move(ins));
            continue;
        }
        target().push_back(std::move(ins));
    }
    while (!stack.empty()) {
        Instr blk = std::move(stack.back());
        stack.pop_back();
        target().push_back(std::move(blk));
    }
    code = std::move(top);
    return true;
}

// ---------------------------------------------------------------- processor

Box controlBox(const ControlDecl& c) {
    Box b;
    b.x = (int)c.x;
    b.w = c.w > 0 ? (int)c.w : 16;
    b.h = c.h > 0 ? (int)c.h : 16;
    switch (c.type) {
        case ControlDecl::Slider:
            b.w = 23;
            b.h = 11;
            b.y = (int)c.y - (int)std::lround(c.offset) - 10;
            break;
        case ControlDecl::RadioButton:
            b.x = (int)c.x;
            b.w = c.w > 0 ? (int)c.w : 18;
            b.h = c.h > 0 ? (int)c.h : 16;
            b.y = (int)c.y - b.h + 1;
            break;
        case ControlDecl::ImageButton:
            b.w = c.w > 0 ? (int)c.w : 74;
            b.h = c.h > 0 ? (int)c.h : 18;
            b.y = (int)c.y - b.h + 1;
            break;
        case ControlDecl::Puzzle:
            b.x = (int)c.x;
            b.y = (int)c.y - 79;
            b.w = 80;
            b.h = 80;
            break;
        case ControlDecl::Menu:
            b.x = (int)c.x;
            b.y = (int)c.y + 1;
            b.w = 119;
            b.h = 213;
            break;
        case ControlDecl::StartupIcon:
            b.x = (int)c.x;
            b.y = (int)c.y;
            b.w = 32;
            b.h = 32;
            break;
        case ControlDecl::Icon:
            b.w = b.h = 32;
            b.y = (int)c.y - 31;
            break;
        case ControlDecl::FinderIcon:
            b.w = c.w > 0 ? (int)c.w : 64;
            b.h = c.h > 0 ? (int)c.h : 48;
            b.x = (int)(c.x + c.dragX);
            b.y = (int)c.y - b.h + 1 + (int)c.dragY;
            break;
        case ControlDecl::Text:
        case ControlDecl::Time:
        case ControlDecl::Date:
            b.y = (int)c.y; // text is drawn from the top, not the bottom
            break;
        default:
            b.y = (int)c.y - b.h + 1;
            break;
    }
    return b;
}

static bool inBox(const Box& b, float x, float y) {
    return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

void Processor::addControl(ControlDecl c, double now) {
    c.startTime = now;
    switch (c.type) {
        case ControlDecl::RadioGroup:
            c.group = radioGroup_++;
            break;
        case ControlDecl::RadioButton:
            if (c.w <= 0) c.w = 18;
            if (c.h <= 0) c.h = 16;
            if (c.name.empty()) c.name = "RadioButton"; // the class default image
            c.group = radioGroup_;
            c.selected = c.hasValue && c.value >= 0.5f;
            break;
        case ControlDecl::Slider:
            c.offset = c.hasValue ? c.value : 24.0;
            c.offset = std::clamp<double>(c.offset, 0, 84);
            break;
        case ControlDecl::ImageButton:
            if (c.name.empty()) c.name = "CancelButton";
            if (c.w <= 0) c.w = 74;
            if (c.h <= 0) c.h = 18;
            break;
        case ControlDecl::StartupIcon:
            if (c.name.empty()) c.name = "StartupIcons";
            break;
        case ControlDecl::Text:
            c.text = c.action;
            break;
        default:
            break;
    }
    controls.push_back(std::move(c));
}

void Processor::setProgram(const Program* p) {
    prog_ = p;
    reset(0);
}

void Processor::reset(double now) {
    cameraX = cameraY = 0;
    scrollX = 256;
    scrollY = 171;
    scale = 1.0f;
    zoom = 0.0f; // "zoom out"/no instruction = normal projection (factor 1)
    mouseX = mouseY = 0;
    imageName.clear();
    sceneName.clear();
    cursorName = "Arrow";
    rampName = "None";
    controls.clear();
    anims_.clear();
    stack_.clear();
    ip_ = 0;
    blockEnd_ = 0;
    blockedUntil_ = now;
    finished = false;
    clock = now;
    mediaTime = 0;
    lastTick_ = now;
    pressed_ = -1;
    radioGroup_ = 0;
    mouseDownState = false;
    prevMouseX_ = mouseX;
    prevMouseY_ = mouseY;

    labels_.clear();
    if (prog_)
        for (size_t i = 0; i < prog_->code.size(); i++)
            if (prog_->code[i].op == Op::Label) labels_.push_back(i);
}

void Processor::jumpTo(const std::string& label) {
    if (!prog_) return;
    for (size_t i : labels_)
        if (prog_->code[i].s0 == label) { ip_ = i + 1; return; }
}

void Processor::updateAnimations(double now) {
    for (auto& a : anims_) {
        if (a.done) continue;
        float t = a.dur > 0 ? (float)((now - a.t0) / a.dur) : 1.0f;
        if (t >= 1.0f) { t = 1.0f; a.done = true; }
        float e = applyCurve(a.curve, t);
        float vx = a.x0 + (a.x1 - a.x0) * e;
        float vy = a.y0 + (a.y1 - a.y0) * e;
        switch (a.kind) {
            case Anim::Cam: cameraX = vx; cameraY = vy; break;
            case Anim::Scr: scrollX = vx; scrollY = vy; break;
            case Anim::Scl: scale = vx; break;
            case Anim::Zm: zoom = vx; break;
            case Anim::Ms: mouseX = vx; mouseY = vy; break;
        }
    }
}

double Processor::startInstruction(const Instr& in, double now) {
    auto beginAnim = [&](Anim::Kind k, float x0, float y0, float x1, float y1) -> double {
        Anim a;
        a.kind = k;
        a.x0 = x0; a.y0 = y0; a.x1 = x1; a.y1 = y1;
        a.t0 = now;
        a.dur = in.hasDur ? in.dur : 0.0;
        a.curve = in.curve;
        a.id = in.s0;
        a.done = false;
        anims_.push_back(a);
        if (in.async) return now;               // runs in background
        return now + a.dur;                     // blocks the program
    };

    switch (in.op) {
        case Op::Camera: return beginAnim(Anim::Cam, cameraX, cameraY, in.x, in.y);
        case Op::Scroll: return beginAnim(Anim::Scr, scrollX, scrollY, in.x, in.y);
        case Op::MouseMove: return beginAnim(Anim::Ms, mouseX, mouseY, in.x, in.y);
        case Op::Scale: {
            float target = in.x;
            return beginAnim(Anim::Scl, scale, 0, target, 0);
        }
        case Op::Zoom: {
            float target = in.x;
            return beginAnim(Anim::Zm, zoom, 0, target, 0);
        }
        default: break;
    }
    return now;
}

bool Processor::tick(double now) {
    clock = now;
    if (now > lastTick_) mediaTime += now - lastTick_;
    lastTick_ = now;
    updateAnimations(now);
    if (finished || !prog_) return false;

    // The scripted mouse is animated by the VM; feed every position change to the
    // input router so pressed controls (slider knob, radio highlight, menu row,
    // finder icon drag) track it exactly like real pointer movement.
    if (mouseX != prevMouseX_ || mouseY != prevMouseY_) {
        prevMouseX_ = mouseX;
        prevMouseY_ = mouseY;
        pointerMove(mouseX, mouseY);
    }
    // A menu is dismissed 0.6 s after the click; it blinks while closing.
    for (auto& c : controls)
        if (c.type == ControlDecl::Menu && c.mouseDown && c.flashStart >= 0 &&
            now - c.flashStart > 0.6) {
            c.mouseDown = false;
            c.flashStart = -1;
        }

    if (now < blockedUntil_) return true;

    // Execute until something blocks again.
    for (int guard = 0; guard < 10000; guard++) {
        if (ip_ >= prog_->code.size()) { finished = true; return false; }
        const Instr& in = prog_->code[ip_];

        switch (in.op) {
            case Op::Block: {
                ip_++;
                double end = now;
                for (const auto& ch : in.body) {
                    if (ch.op == Op::Add) {
                        addControl(ch.ctl, now);
                    } else if (ch.op == Op::Let) {
                        if (ch.s0 == "image") imageName = ch.s1;
                        else if (ch.s0 == "scene") sceneName = ch.s1;
                        else if (ch.s0 == "cursor") cursorName = ch.s1;
                        else if (ch.s0 == "ramp") rampName = ch.s1;
                    } else {
                        double e = startInstruction(ch, now);
                        if (e > end) end = e;
                    }
                }
                if (end > now) { blockedUntil_ = end; return true; }
                break;
            }
            case Op::Let:
                ip_++;
                if (in.s0 == "image") imageName = in.s1;
                else if (in.s0 == "scene") sceneName = in.s1;
                else if (in.s0 == "cursor") cursorName = in.s1;
                else if (in.s0 == "ramp") rampName = in.s1;
                break;
            case Op::Add: {
                ip_++;
                addControl(in.ctl, now);
                break;
            }
            case Op::Camera: case Op::Scroll: case Op::MouseMove:
            case Op::Scale: case Op::Zoom: {
                ip_++;
                double end = startInstruction(in, now);
                if (end > now) { blockedUntil_ = end; return true; }
                break;
            }
            case Op::MouseDown:
                ip_++;
                pointerDown(mouseX, mouseY);
                break;
            case Op::MouseUp:
                ip_++;
                pointerUp(mouseX, mouseY);
                break;
            case Op::MouseClick: {
                ip_++;
                pointerClick(mouseX, mouseY);
                break;
            }
            case Op::Keyboard: {
                ip_++;
                // The only text field in the shipped programs is MacWrite's document.
                for (auto it = controls.rbegin(); it != controls.rend(); ++it) {
                    if (it->type != ControlDecl::Text) continue;
                    for (size_t i = 0; i < in.s0.size(); i++) {
                        if (in.s0[i] == '\\' && i + 1 < in.s0.size() && in.s0[i + 1] == 'n') {
                            it->text += '\n';
                            i++;
                        } else {
                            it->text += in.s0[i];
                        }
                    }
                    // The original text setter resets the selection to 0.
                    it->selStart = it->selEnd = 0;
                    it->textTracking = false;
                    break;
                }
                break;
            }
            case Op::Wait:
                ip_++;
                blockedUntil_ = now + in.dur;
                return true;
            case Op::WaitFor: {
                ip_++;
                double end = now;
                for (auto& a : anims_)
                    if (!a.id.empty() && a.id == in.s0) end = std::max(end, a.t0 + a.dur);
                if (end > now) { blockedUntil_ = end; return true; }
                break;
            }
            case Op::WaitUntil: {
                ip_++;
                double end = now;
                for (auto& a : anims_)
                    if (!a.id.empty() && a.id == in.s0)
                        end = std::max(end, a.t0 + a.dur - in.dur);
                if (end > now) { blockedUntil_ = end; return true; }
                break;
            }
            case Op::Cancel: {
                ip_++;
                for (auto& a : anims_)
                    if (!a.id.empty() && a.id == in.s0) a.done = true;
                break;
            }
            case Op::Gosub:
                stack_.push_back({ip_ + 1, blockEnd_});
                ip_++;
                jumpTo(in.s0);
                break;
            case Op::Goto:
                jumpTo(in.s0);
                break;
            case Op::Label:
                ip_++;
                break;
            case Op::Return:
                if (!stack_.empty()) { ip_ = stack_.back().ip; stack_.pop_back(); }
                else ip_++;
                break;
            case Op::Exit:
                finished = true;
                return false;
            case Op::ResetControls:
                ip_++;
                for (auto& c : controls) {
                    c.startTime = now;
                    if (c.type == ControlDecl::Puzzle) {
                        static const int kTiles[16] = {1, 2, 3, 13, 11, 0, 4, 6,
                                                       8, 7, 12, 14, 9, 15, 5, 10};
                        std::copy(kTiles, kTiles + 16, c.tiles);
                    } else if (c.type == ControlDecl::StartupIcon) {
                        c.happy = false;
                    } else if (c.type == ControlDecl::Menu) {
                        c.mouseDown = false;
                        c.flashStart = -1;
                        c.hilitedRow = -1;
                    } else if (c.type == ControlDecl::Text) {
                        c.selStart = c.selEnd = 0;
                        c.textTracking = false;
                    }
                }
                break;
        }
    }
    return true;
}

// ---------------------------------------------------------------- pointer router

static void updateMenuRow(ControlDecl& c, float y) {
    int row = (int)std::floor((341.0f - y) / 16.0f);
    if (row < 8 || row > 19 || row == 13) row = -1;
    c.hilitedRow = row;
}

// Desk Accessories' 15 puzzle: clicking a tile that shares a row or column with
// the blank slides the whole line, moving the blank to the clicked cell.
static void puzzleClick(ControlDecl& c, float px, float py) {
    Box b = controlBox(c);
    int col = (int)std::floor((px - b.x) / 20.0f);
    int row = (int)std::floor((py - b.y) / 20.0f);
    if (col < 0 || col > 3 || row < 0 || row > 3) return;
    int cell = row * 4 + col, blank = -1;
    for (int i = 0; i < 16; i++)
        if (c.tiles[i] == 0) blank = i;
    if (blank < 0 || blank == cell) return;
    int br = blank / 4, bc = blank % 4;
    if (col == bc) {
        if (row < br)
            for (int r = br; r > row; --r) c.tiles[r * 4 + col] = c.tiles[(r - 1) * 4 + col];
        else
            for (int r = br; r < row; ++r) c.tiles[r * 4 + col] = c.tiles[(r + 1) * 4 + col];
        c.tiles[cell] = 0;
    } else if (row == br) {
        if (col < bc)
            for (int k = bc; k > col; --k) c.tiles[row * 4 + k] = c.tiles[row * 4 + k - 1];
        else
            for (int k = bc; k < col; ++k) c.tiles[row * 4 + k] = c.tiles[row * 4 + k + 1];
        c.tiles[cell] = 0;
    }
}

// The desk accessory's document canvas is cleared by a new program, so line art
// only exists for the lifetime of one scene (handled by Scene).

void Processor::pointerDown(float x, float y) {
    mouseX = x;
    mouseY = y;
    mouseDownState = true;
    pressed_ = -1;
    for (int i = (int)controls.size() - 1; i >= 0; --i) {
        // Cursor rects and group markers are not click targets.
        if (controls[i].type == ControlDecl::CursorRect ||
            controls[i].type == ControlDecl::RadioGroup)
            continue;
        if (inBox(controlBox(controls[i]), x, y)) {
            pressed_ = i;
            break;
        }
    }
    if (pressed_ < 0) return;
    ControlDecl& c = controls[pressed_];
    switch (c.type) {
        case ControlDecl::RadioButton:
            c.selected = true;
            c.highlighted = true;
            break;
        case ControlDecl::Button:
        case ControlDecl::ImageButton:
            c.highlighted = true;
            c.tracking = true;
            break;
        case ControlDecl::Slider:
            c.dragStart = y + c.offset;
            c.tracking = true;
            break;
        case ControlDecl::Menu:
            c.mouseDown = true;
            c.flashStart = -1;
            updateMenuRow(c, y);
            break;
        case ControlDecl::Puzzle:
            puzzleClick(c, x, y);
            break;
        case ControlDecl::FinderIcon:
            c.selected = true;
            c.pressX = x;
            c.pressY = y;
            c.dragX = c.dragY = 0;
            break;
        case ControlDecl::StartupIcon:
            c.happy = true;
            break;
        case ControlDecl::Canvas:
            c.penX = x;
            c.penY = y;
            c.strokes.insert(c.strokes.end(), {x, y, x, y});
            break;
        default:
            break;
    }
}

void Processor::pointerMove(float x, float y) {
    mouseX = x;
    mouseY = y;
    for (size_t i = 0; i < controls.size(); i++) {
        ControlDecl& c = controls[i];
        bool held = (int)i == pressed_;
        switch (c.type) {
            case ControlDecl::RadioButton:
                if (held && c.highlighted) c.highlighted = inBox(controlBox(c), x, y);
                break;
            case ControlDecl::Button:
            case ControlDecl::ImageButton:
                if (held && c.tracking) c.highlighted = inBox(controlBox(c), x, y);
                break;
            case ControlDecl::Slider:
                if (held && c.tracking) {
                    c.offset = std::clamp<double>(c.dragStart - y, -1.0, 85.0);
                }
                break;
            case ControlDecl::Menu:
                updateMenuRow(c, y);
                break;
            case ControlDecl::FinderIcon:
                if (held) {
                    c.dragX = x - c.pressX;
                    c.dragY = y - c.pressY;
                    if (std::fabs(c.dragX) > 3 || std::fabs(c.dragY) > 3) c.tracking = true;
                }
                break;
            case ControlDecl::Canvas:
                if (held && c.penX >= 0 && (x != c.penX || y != c.penY)) {
                    c.strokes.insert(c.strokes.end(), {c.penX, c.penY, x, y});
                    c.penX = x;
                    c.penY = y;
                }
                break;
            default:
                break;
        }
    }
}

void Processor::pointerUp(float x, float y) {
    mouseX = x;
    mouseY = y;
    mouseDownState = false;
    if (pressed_ >= 0 && pressed_ < (int)controls.size()) {
        ControlDecl& c = controls[pressed_];
        bool inside = inBox(controlBox(c), x, y);
        switch (c.type) {
            case ControlDecl::RadioButton: {
                bool wasHighlighted = c.highlighted;
                if (inside && wasHighlighted) {
                    for (auto& o : controls)
                        if (o.type == ControlDecl::RadioButton && o.group == c.group)
                            o.selected = (&o == &c);
                    if (!c.action.empty()) firedActions.push_back(c.action);
                } else {
                    c.selected = false;
                }
                c.highlighted = false;
                break;
            }
            case ControlDecl::Button:
            case ControlDecl::ImageButton:
                if (inside && c.highlighted && !c.action.empty())
                    firedActions.push_back(c.action);
                c.highlighted = false;
                c.tracking = false;
                break;
            case ControlDecl::Slider:
                if (c.tracking) {
                    c.offset = std::floor((c.offset + 5.0) / 12.0) * 12.0;
                    c.offset = std::clamp<double>(c.offset, 0.0, 84.0);
                    c.dragStart = 0;
                }
                c.tracking = false;
                break;
            case ControlDecl::Menu:
                // Keep the menu open for the closing flash; tick() hides it.
                if (c.mouseDown) c.flashStart = clock;
                break;
            case ControlDecl::FinderIcon: {
                c.tracking = false;
                c.selected = false;
                // Dropping the folder on the trash icon makes it disappear.
                for (auto& o : controls) {
                    if (o.type != ControlDecl::FinderIcon || &o == &c) continue;
                    if (o.name.find("Trash") == std::string::npos) continue;
                    if (inBox(controlBox(o), x, y)) c.trashed = true;
                }
                c.dragX = c.dragY = 0;
                break;
            }
            default:
                break;
        }
    }
    // A canvas is only stroked while the button is held; release ends the path.
    for (auto& c : controls)
        if (c.type == ControlDecl::Canvas && !mouseDownState) c.penX = c.penY = -1;
    pressed_ = -1;
}

void Processor::pointerClick(float x, float y) {
    pointerDown(x, y);
    pointerUp(x, y);
}
