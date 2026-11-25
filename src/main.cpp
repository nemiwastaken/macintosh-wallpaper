#include "image.h"
#include "overlay.h"
#include "program.h"
#include "renderer.h"
#include "scene.h"
#include "theme.h"
#include <epoxy/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <algorithm>
#include <csignal>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <functional>
#include <langinfo.h>
#include <unistd.h>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#ifdef HAVE_LAYER_SHELL
#include "layer_shell.h"
#endif

#ifndef RESOURCE_DIR
#define RESOURCE_DIR "assets/resources"
#endif
#ifndef SHADER_DIR
#define SHADER_DIR "shaders"
#endif

namespace fs = std::filesystem;

struct Options {
    std::string assets = RESOURCE_DIR;
    std::string shaders = SHADER_DIR;
    std::string program = "IconGardenWallpaper";
    std::string theme = "spectrum";
    std::string appearance = "auto";
    std::string presentation = "wallpaper"; // wallpaper | screensaver
    std::string layer = "background";       // layer-shell layer
    std::string dump, dumpMask;
    int fps = 30;
    int monitor = -1;
    int width = 0, height = 0;
    bool windowed = false, cycle = false, paused = false;
    bool allMonitors = false, selftest = false, listOnly = false, layerShell = false;
    bool gui = false;
    float speed = 1.0f;
    bool hasCam = false, hasScroll = false;
    float camX = 0, camY = 0, scrX = 0, scrY = 0;
    double at = 0;
};

static void usage() {
    printf(
        "macintosh-wallpaper — native port of the macOS \"Macintosh\" dynamic wallpaper\n\n"
        "Usage: macintosh-wallpaper [options]\n"
        "  --assets DIR        resource directory (default: %s)\n"
        "  --shaders DIR       shader directory (default: %s)\n"
        "  --program NAME      program to run (default: IconGardenWallpaper)\n"
        "  --cycle             rotate through every program (screensaver)\n"
        "  --theme NAME        green|yellow|orange|red|purple|blue|gray|darkGray|spectrum|random\n"
        "  --appearance MODE   light|dark|auto (default: auto)\n"
        "  --presentation MODE wallpaper|screensaver (default: wallpaper)\n"
        "  --layer-shell       run as a Wayland wallpaper (wlr-layer-shell, one per output)\n"
        "  --layer NAME        layer-shell layer: background (default), bottom, top\n"
        "  --fps N             frame rate cap (default: 30)\n"
        "  --monitor N         monitor index for fullscreen\n"
        "  --all-monitors      one borderless X11 window spanning all monitors\n"
        "  --windowed --width N --height N\n"
        "  --gui               open the settings window (Tab toggles it in --windowed)\n"
        "  --paused            start paused (Space toggles, Esc quits)\n"
        "  --speed F           playback speed (default: 1)\n"
        "  --dump-frame FILE   render one frame to PNG and exit\n"
        "  --dump-mask FILE    dump the composed 1-bit screen and exit\n"
        "  --at SECONDS        fast-forward before dumping\n"
        "  --selftest          run built-in checks and exit\n"
        "  --camera X,Y        override camera angles (debug)\n"
        "  --scroll X,Y        override scroll position (debug)\n"
        "  --list-programs / --list-themes / --help\n"
        "  live settings: $XDG_RUNTIME_DIR/macintosh-wallpaper-control (theme, program,\n"
        "                 appearance, presentation, speed, fps, paused; see\n"
        "                 tools/integration/control.sh)\n",
        RESOURCE_DIR, SHADER_DIR);
}

static bool systemIsDark() {
    FILE* p = popen("gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null", "r");
    if (!p) return false;
    char buf[256] = {0};
    if (fgets(buf, sizeof buf, p)) {
        pclose(p);
        return strstr(buf, "dark") != nullptr;
    }
    pclose(p);
    p = popen("gsettings get org.gnome.desktop.interface gtk-theme 2>/dev/null", "r");
    if (!p) return false;
    bool dark = false;
    if (fgets(buf, sizeof buf, p)) dark = strstr(buf, "dark") != nullptr;
    pclose(p);
    return dark;
}

// All runtime state of one wallpaper instance, independent of the window backend.
struct Session {
    Options* o = nullptr;
    std::map<std::string, Program>* programs = nullptr;
    Scene scene;
    Processor proc;
    std::vector<std::string> sequence;
    size_t seqIndex = 0;

    bool dark = false, wallpaperMode = true, spectrum = true, randomTheme = false;
    ColorScheme fixedScheme = ColorScheme::Green;
    std::mt19937 rng{std::random_device{}()};

    Color current;
    Shading shading;
    float smoothedSpeed = 1.0f, smoothedContrast = 0.2f;
    bool use24h = true, timeModeFromControls = false;
    double caretInterval = 0.5333;
    double simTime = 0, nextProgramAt = -1;
    bool paused = false;
    bool finished = false;

    void start(std::map<std::string, Program>& progs, std::vector<std::string> seq) {
        programs = &progs;
        sequence = std::move(seq);
        proc.setProgram(&(*programs)[sequence[0]]);
        current = spectrum ? spectrumColor(0.0) : schemeColor(fixedScheme, dark);
        shading = makeShading(current, dark, wallpaperMode ? 0.666f : 0.2f);
        smoothedSpeed = wallpaperMode ? 0.0f : 1.0f;
        smoothedContrast = wallpaperMode ? 0.666f : 0.2f;
        if (o->at > 0) {
            for (double t = 0; t < o->at; t += 1.0 / 60.0) {
                proc.tick(t);
                Color tg = spectrum ? spectrumColor(proc.mediaTime)
                                    : schemeColor(fixedScheme, dark);
                current = current.mix(tg, 0.04f);
            }
            simTime = o->at;
        }
    }

    void fireActions() {
        if (!timeModeFromControls && !proc.controls.empty()) {
            for (const auto& c : proc.controls) {
                if (!c.selected) continue;
                if (c.action == "time24Hour") { use24h = true; timeModeFromControls = true; }
                else if (c.action == "time12Hour") { use24h = false; timeModeFromControls = true; }
            }
        }
        for (const auto& act : proc.firedActions) {
            if (act == "caretSlow") caretInterval = 0.8;
            else if (act == "caretNormal") caretInterval = 0.5333;
            else if (act == "caretFast") caretInterval = 0.27;
            else if (act == "time24Hour") use24h = true;
            else if (act == "time12Hour") use24h = false;
            else if (act.size() == 1 && act[0] >= '0' && act[0] <= '9') {
                // Desk Accessories calculator: digits land in the right-aligned
                // text field (the initial "0" is replaced by the first digit).
                for (auto it = proc.controls.rbegin(); it != proc.controls.rend(); ++it) {
                    if (it->type != ControlDecl::Text) continue;
                    if (it->text == "0") it->text = act;
                    else it->text += act;
                    size_t maxLen = it->hasValue && it->value > 0 ? (size_t)it->value : 0;
                    if (maxLen && it->text.size() > maxLen) it->text.erase(0, it->text.size() - maxLen);
                    break;
                }
            }
        }
        proc.firedActions.clear();
    }

    void update(double dt) {
        if (!paused && o->speed > 0) simTime += dt * o->speed;
        bool alive = proc.tick(simTime);
        if (!alive && o->cycle && nextProgramAt < 0) nextProgramAt = simTime + 1.5;
        // In the settings window a finished program restarts itself; most
        // screensaver programs end on `scale out`, which would leave a blank frame.
        if (!alive && o->gui && !o->cycle && nextProgramAt < 0) nextProgramAt = simTime + 2.0;
        if (!o->cycle && o->gui && nextProgramAt > 0 && simTime >= nextProgramAt) {
            proc.setProgram(&(*programs)[sequence[seqIndex]]);
            scene.resetCanvas();
            nextProgramAt = -1;
            timeModeFromControls = false;
        }
        if (o->cycle && nextProgramAt > 0 && simTime >= nextProgramAt) {
            seqIndex = (seqIndex + 1) % sequence.size();
            proc.setProgram(&(*programs)[sequence[seqIndex]]);
            scene.resetCanvas();
            nextProgramAt = -1;
            timeModeFromControls = false;
            if (randomTheme) {
                static const ColorScheme pool[] = {ColorScheme::Green, ColorScheme::Yellow,
                                                   ColorScheme::Orange, ColorScheme::Red,
                                                   ColorScheme::Purple, ColorScheme::Blue};
                fixedScheme = pool[rng() % 6];
            }
        }
        fireActions();

        Color target = spectrum ? spectrumColor(proc.mediaTime)
                                : schemeColor(fixedScheme, dark);
        current = current.mix(target, 0.04f);
        float speedTarget = (paused || wallpaperMode) ? 0.0f : 1.0f;
        float contrastTarget = (wallpaperMode || (paused && proc.scale >= 1.0f)) ? 0.666f : 0.2f;
        smoothedSpeed = smoothedSpeed * 0.96f + speedTarget * 0.04f;
        if (std::fabs(smoothedContrast - contrastTarget) > 0.01f)
            smoothedContrast = smoothedContrast * 0.96f + contrastTarget * 0.04f;
        else
            smoothedContrast = contrastTarget;
        shading = makeShading(current, dark, smoothedContrast);

        // Compose the scene; the pointer state drives cursor, canvas and widgets.
        scene.compose(proc.imageName, proc.controls, proc.cursorName, simTime, use24h,
                      caretInterval, proc.mouseX, proc.mouseY, proc.mouseDownState);
    }

    void draw(Renderer& r, int w, int h, float scale = 1.0f) {
        r.setSize((int)(w * scale), (int)(h * scale));
        float camX = o->hasCam ? o->camX : proc.cameraX;
        float camY = o->hasCam ? o->camY : proc.cameraY;
        float scX = o->hasScroll ? o->scrX : proc.scrollX;
        float scY = o->hasScroll ? o->scrY : proc.scrollY;
        r.draw(scene.mask(), shading, camX, camY, scX, scY, proc.scale, proc.zoom,
               proc.rampName, smoothedSpeed);
    }
};

// ---- live settings ---------------------------------------------------------
// Shared by --gui and by the control file, so both accept the same values.

static bool setTheme(Session& s, const std::string& name) {
    ColorScheme scheme;
    if (name != "spectrum" && name != "random" && !parseScheme(name, scheme)) return false;
    s.o->theme = name;
    s.spectrum = name == "spectrum" || name == "random";
    s.randomTheme = name == "random";
    if (!s.spectrum) {
        s.fixedScheme = scheme;
    } else if (s.randomTheme) {
        static const ColorScheme pool[] = {ColorScheme::Green, ColorScheme::Yellow,
                                           ColorScheme::Orange, ColorScheme::Red,
                                           ColorScheme::Purple, ColorScheme::Blue};
        s.fixedScheme = pool[s.rng() % 6];
    }
    return true;
}

static std::string trimmed(const std::string& in) {
    size_t b = in.find_first_not_of(" \t\r");
    if (b == std::string::npos) return {};
    size_t e = in.find_last_not_of(" \t\r");
    return in.substr(b, e - b + 1);
}

// One `key=value` line from the control file. False means the line was bad.
static bool applySetting(Session& s, std::map<std::string, Program>& programs,
                         const std::string& key, const std::string& value) {
    Options& o = *s.o;
    if (key == "program") {
        if (value == "cycle") {
            std::vector<std::string> all;
            for (auto& kv : programs) all.push_back(kv.first);
            o.cycle = true;
            s.start(programs, all);
        } else {
            if (!programs.count(value)) return false;
            o.cycle = false;
            o.program = value;
            s.start(programs, {value});
        }
        s.scene.resetCanvas();
        s.seqIndex = 0;
        s.nextProgramAt = -1;
        s.timeModeFromControls = false;
        return true;
    }
    if (key == "theme") return setTheme(s, value);
    if (key == "appearance") {
        if (value != "light" && value != "dark" && value != "auto") return false;
        o.appearance = value;
        s.dark = value == "dark" || (value == "auto" && systemIsDark());
        return true;
    }
    if (key == "presentation") {
        if (value != "wallpaper" && value != "screensaver") return false;
        o.presentation = value;
        s.wallpaperMode = value == "wallpaper";
        return true;
    }
    if (key == "speed") {
        char* end = nullptr;
        float v = strtof(value.c_str(), &end);
        if (end == value.c_str()) return false;
        o.speed = std::clamp(v, 0.25f, 4.0f);
        return true;
    }
    if (key == "fps") {
        char* end = nullptr;
        long v = strtol(value.c_str(), &end, 10);
        if (end == value.c_str()) return false;
        o.fps = (int)std::clamp(v, 5L, 120L);
        return true;
    }
    if (key == "paused") {
        if (value == "1" || value == "true" || value == "on") s.paused = true;
        else if (value == "0" || value == "false" || value == "off") s.paused = false;
        else return false;
        return true;
    }
    return false;
}

static int listThemes() {
    for (auto n : {"green", "yellow", "orange", "red", "purple", "blue", "gray", "darkGray",
                   "spectrum", "random"})
        printf("%s\n", n);
    return 0;
}

static int listPrograms(std::map<std::string, Program>& programs) {
    std::vector<std::string> names;
    for (auto& kv : programs) names.push_back(kv.first);
    std::sort(names.begin(), names.end());
    for (auto& n : names) printf("%s\n", n.c_str());
    return 0;
}

// When installed, the compiled-in source paths do not exist; fall back to the
// share/ directory next to the binary or under /usr/local/share.
static std::string resolveDir(const std::string& given, const std::string& sub) {
    std::error_code ec;
    if (fs::exists(given, ec)) return given;
    char exe[4096] = {0};
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        fs::path p = fs::path(std::string(exe, (size_t)n)).parent_path();
        for (fs::path base : {p.parent_path() / "share" / "macintosh-wallpaper",
                              p / ".." / "share" / "macintosh-wallpaper"}) {
            fs::path cand = base / sub;
            if (fs::exists(cand, ec)) return cand.string();
        }
    }
    for (const char* base : {"/usr/local/share/macintosh-wallpaper",
                             "/usr/share/macintosh-wallpaper"}) {
        fs::path cand = fs::path(base) / sub;
        if (fs::exists(cand, ec)) return cand.string();
    }
    return given;
}

static void loadPrograms(const Options& o, std::map<std::string, Program>& programs) {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(o.assets, ec)) {
        if (e.path().extension() != ".program") continue;
        Program p;
        p.name = e.path().stem().string();
        FILE* f = fopen(e.path().c_str(), "rb");
        if (!f) continue;
        std::string text;
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
        fclose(f);
        p.parse(text);
        programs[p.name] = std::move(p);
    }
}

#ifdef HAVE_LAYER_SHELL
// The desktop shell (amane) hides its own picture while this file points at a live
// wallpaper, so a shell restart cannot bury the animation under a static one.
static std::string pidFilePath() {
    const char* rt = getenv("XDG_RUNTIME_DIR");
    return std::string(rt && *rt ? rt : "/tmp") + "/macintosh-wallpaper.pid";
}

// a tiny picture of the current paper colour, polled by the desktop shell's palette
static std::string frameFilePath() {
    const char* rt = getenv("XDG_RUNTIME_DIR");
    return std::string(rt && *rt ? rt : "/tmp") + "/macintosh-wallpaper-frame.png";
}

// the same colours as text, so a shell needs no PNG decoder to follow them
static std::string paletteFilePath() {
    const char* rt = getenv("XDG_RUNTIME_DIR");
    return std::string(rt && *rt ? rt : "/tmp") + "/macintosh-wallpaper-palette";
}

// live settings (theme, program, …), rewritten by tools/integration/control.sh
static std::string controlFilePath() {
    const char* rt = getenv("XDG_RUNTIME_DIR");
    return std::string(rt && *rt ? rt : "/tmp") + "/macintosh-wallpaper-control";
}

static std::string hexColor(Color c) {
    auto enc = [](float v) {
        return (int)std::lround(std::clamp(linearToSrgb(v), 0.0f, 1.0f) * 255.0f);
    };
    char buf[8];
    snprintf(buf, sizeof buf, "#%02x%02x%02x", enc(c.r), enc(c.g), enc(c.b));
    return buf;
}

static void writePaletteText(const Shading& sh, const std::string& path) {
    std::string tmp = path + ".tmp";
    if (FILE* f = fopen(tmp.c_str(), "w")) {
        fprintf(f, "paper_top=%s\npaper_bottom=%s\nink=%s\n", hexColor(sh.bgTop).c_str(),
                hexColor(sh.bgBottom).c_str(), hexColor(sh.ink).c_str());
        fclose(f);
        std::rename(tmp.c_str(), path.c_str());
    }
}

// The shell quantises this image for its accent/background colours, so the whole
// shell follows the wallpaper's current colour (green, spectrum, …).
static void writePaletteFrame(const Shading& sh, const std::string& path) {
    const int W = 24, H = 14, INK_ROWS = 2;
    std::vector<uint8_t> rgb((size_t)W * H * 3);
    auto enc = [](float c) -> uint8_t {
        return (uint8_t)std::lround(std::clamp(linearToSrgb(c), 0.0f, 1.0f) * 255.0f);
    };
    for (int y = 0; y < H; y++) {
        Color c = y >= H - INK_ROWS
                      ? sh.ink
                      : sh.bgTop.mix(sh.bgBottom, (float)y / (float)(H - INK_ROWS - 1));
        for (int x = 0; x < W; x++) {
            size_t i = ((size_t)y * W + x) * 3;
            rgb[i] = enc(c.r);
            rgb[i + 1] = enc(c.g);
            rgb[i + 2] = enc(c.b);
        }
    }
    std::string tmp = path + ".tmp";
    if (writePng(tmp, W, H, rgb.data())) std::rename(tmp.c_str(), path.c_str());
}

struct PidFile {
    std::string path;
    std::string frame;
    std::string palette;
    // a one-shot dump is not a wallpaper: the shell must not hide its picture for it
    explicit PidFile(bool enabled) : path(enabled ? pidFilePath() : std::string()) {
        if (path.empty()) return;
        // a second --layer-shell must not steal the marker from the running wallpaper
        if (FILE* f = fopen(path.c_str(), "r")) {
            int other = 0;
            bool live = fscanf(f, "%d", &other) == 1 && other != (int)getpid() &&
                        kill(other, 0) == 0;
            fclose(f);
            if (live) {
                path.clear();
                return;
            }
        }
        frame = frameFilePath();
        palette = paletteFilePath();
        FILE* f = fopen(path.c_str(), "w");
        if (f) {
            fprintf(f, "%d\n", (int)getpid());
            fclose(f);
        }
    }
    ~PidFile() {
        if (path.empty()) return;
        // only remove our own file, never one a newer instance wrote
        FILE* f = fopen(path.c_str(), "r");
        if (!f) return;
        int pid = 0;
        int ok = fscanf(f, "%d", &pid);
        fclose(f);
        if (ok == 1 && pid == (int)getpid()) {
            unlink(path.c_str());
            unlink(frame.c_str());
            unlink(palette.c_str());
        }
    }
};

static volatile sig_atomic_t g_stop = 0;
static void onStopSignal(int) { g_stop = 1; }

// Wallpaper mode: one background surface per output, paced at --fps.
static int runLayerShell(Options& o, std::map<std::string, Program>& programs,
                         std::vector<std::string> sequence, bool dark) {
    LayerShell shell;
    int layer = o.layer == "background" ? 0 : o.layer == "top" ? 2 : o.layer == "overlay" ? 3 : 1;
    if (!shell.init(layer)) return 1;

    Renderer renderer;
    if (!renderer.init(o.shaders)) return 1;

    Session s;
    s.o = &o;
    s.scene.init(o.assets);
    s.dark = dark;
    s.wallpaperMode = o.presentation != "screensaver";
    s.spectrum = o.theme == "spectrum" || o.theme == "random";
    s.randomTheme = o.theme == "random";
    if (!s.spectrum && !parseScheme(o.theme, s.fixedScheme)) return 1;
    if (s.randomTheme) {
        static const ColorScheme pool[] = {ColorScheme::Green, ColorScheme::Yellow,
                                           ColorScheme::Orange, ColorScheme::Red,
                                           ColorScheme::Purple, ColorScheme::Blue};
        s.fixedScheme = pool[s.rng() % 6];
    }
    s.paused = o.paused;
    if (const char* fmt = nl_langinfo(T_FMT)) s.use24h = strstr(fmt, "%H") || strstr(fmt, "%k");
    s.start(programs, std::move(sequence));

    if (getenv("MW_DEBUG")) {
        fprintf(stderr, "layer-shell: %zu output(s)\n", shell.outputs().size());
        for (auto& out : shell.outputs())
            fprintf(stderr, "  configured=%d %dx%d scale=%d\n", out.configured ? 1 : 0,
                    out.width, out.height, out.scale);
    }
    PidFile pid(o.dump.empty());
    // Live settings, polled twice a second. A one-shot dump stays deterministic.
    std::string control = o.dump.empty() ? controlFilePath() : std::string();
    std::string controlSeen = "\x01";
    std::map<std::string, std::string> controlApplied;
    double nextControl = 0;
    signal(SIGTERM, onStopSignal);
    signal(SIGINT, onStopSignal);
    Color lastPaper, lastInk;
    bool havePaper = false;
    double nextPaper = 0;
    bool dumped = false;
    double t0 = 0, last = 0;
    struct timespec ts0;
    clock_gettime(CLOCK_MONOTONIC, &ts0);
    t0 = ts0.tv_sec + ts0.tv_nsec * 1e-9;
    last = t0;
    while (!g_stop) {
        if (!shell.dispatch()) break;
        for (auto& out : shell.outputs()) {
            if (out.configured && out.needResize) shell.resize(out);
        }
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        double now = ts.tv_sec + ts.tv_nsec * 1e-9;
        double dt = std::min(now - last, 0.25);
        last = now;
        if (!control.empty() && now >= nextControl) {
            nextControl = now + 0.5;
            std::string text;
            if (FILE* f = fopen(control.c_str(), "r")) {
                char buf[4096];
                size_t n;
                while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
                fclose(f);
            }
            if (text != controlSeen) {
                controlSeen = text;
                size_t pos = 0;
                while (pos < text.size()) {
                    size_t eol = text.find('\n', pos);
                    std::string line =
                        text.substr(pos, eol == std::string::npos ? std::string::npos
                                                                 : eol - pos);
                    pos = eol == std::string::npos ? text.size() : eol + 1;
                    line = trimmed(line);
                    if (line.empty() || line[0] == '#') continue;
                    size_t eq = line.find('=');
                    if (eq == std::string::npos) {
                        fprintf(stderr, "macintosh-wallpaper: bad control line \"%s\"\n",
                                line.c_str());
                        continue;
                    }
                    std::string key = trimmed(line.substr(0, eq));
                    std::string value = trimmed(line.substr(eq + 1));
                    // Only a changed line is applied: editing one setting must not
                    // restart the program named by another.
                    if (controlApplied[key] == value) continue;
                    if (!applySetting(s, programs, key, value)) {
                        fprintf(stderr, "macintosh-wallpaper: bad control line \"%s\"\n",
                                line.c_str());
                    } else {
                        controlApplied[key] = value;
                        if (getenv("MW_DEBUG"))
                            fprintf(stderr, "control: %s=%s\n", key.c_str(), value.c_str());
                    }
                }
            }
        }
        s.update(dt);
        // keep the shell's palette in step with the paper colour (spectrum changes it)
        if (!pid.frame.empty() && now >= nextPaper) {
            nextPaper = now + 2.0;
            const Color& paper = s.shading.bgBottom;
            const Color& ink = s.shading.ink;
            bool changed = !havePaper || std::fabs(paper.r - lastPaper.r) > 0.01f ||
                           std::fabs(paper.g - lastPaper.g) > 0.01f ||
                           std::fabs(paper.b - lastPaper.b) > 0.01f ||
                           std::fabs(ink.r - lastInk.r) > 0.01f ||
                           std::fabs(ink.g - lastInk.g) > 0.01f ||
                           std::fabs(ink.b - lastInk.b) > 0.01f;
            if (changed) {
                writePaletteFrame(s.shading, pid.frame);
                writePaletteText(s.shading, pid.palette);
                lastPaper = paper;
                lastInk = ink;
                havePaper = true;
            }
        }
        for (auto& out : shell.outputs()) {
            if (!out.configured || !out.eglSurface) continue;
            if (!shell.makeCurrent(out)) continue;
            int scale = out.scale > 0 ? out.scale : 1;
            glViewport(0, 0, out.width * scale, out.height * scale);
            s.draw(renderer, out.width, out.height, (float)scale);
            if (!o.dump.empty()) {
                std::vector<uint8_t> rgb;
                renderer.readRgb(rgb);
                writePng(o.dump, renderer.width(), renderer.height(), rgb.data());
                printf("wrote %s (%dx%d)\n", o.dump.c_str(), renderer.width(),
                       renderer.height());
                dumped = true;
                break;
            }
            shell.swap(out);
        }
        if (dumped) break;
        if (o.dump.empty()) {
            struct timespec e;
            clock_gettime(CLOCK_MONOTONIC, &e);
            double spent = (e.tv_sec + e.tv_nsec * 1e-9) - now;
            double spare = 1.0 / o.fps - spent;
            if (spare > 0.001) {
                struct timespec sl{0, (long)(spare * 1e9)};
                nanosleep(&sl, nullptr);
            }
        }
    }
    renderer.shutdown();
    return 0;
}
#endif

static void glfwError(int code, const char* desc) {
    fprintf(stderr, "glfw error %d: %s\n", code, desc);
}

constexpr int kGuiRows = 7;

struct Keys {
    bool* paused;
    Session* session = nullptr;
    // Settings overlay (GLFW window only).
    Overlay* overlay = nullptr;
    bool* guiOpen = nullptr;
    int* guiRow = nullptr;
    bool guiAlways = false;
    std::function<void(int)> guiAdjust;
    std::function<void()> guiPause;
};

static void scenePoint(GLFWwindow* win, double x, double y, float& sx, float& sy) {
    // glfwGetCursorPos is in window (logical) coordinates, so map with the window
    // size, not the framebuffer size (they differ on HiDPI).
    int ww = 1, wh = 1;
    glfwGetWindowSize(win, &ww, &wh);
    if (ww < 1) ww = 1;
    if (wh < 1) wh = 1;
    sx = (float)(x * 512.0 / ww);
    sy = (float)(y * 342.0 / wh);
}

static void onCursorPos(GLFWwindow* win, double x, double y) {
    Keys* k = (Keys*)glfwGetWindowUserPointer(win);
    if (!k || !k->session || k->session->wallpaperMode) return;
    float sx, sy;
    scenePoint(win, x, y, sx, sy);
    if (k->session->proc.mouseDownState) k->session->proc.pointerMove(sx, sy);
}

static void onMouseButton(GLFWwindow* win, int button, int action, int) {
    Keys* k = (Keys*)glfwGetWindowUserPointer(win);
    if (!k || !k->session || button != GLFW_MOUSE_BUTTON_LEFT) return;
    if (k->guiOpen && *k->guiOpen && k->overlay) {
        if (action != GLFW_PRESS) return;
        int fw = 1, fh = 1, ww = 1, wh = 1;
        glfwGetFramebufferSize(win, &fw, &fh);
        glfwGetWindowSize(win, &ww, &wh);
        double x, y;
        glfwGetCursorPos(win, &x, &y);
        int px = (int)(x * fw / std::max(1, ww));
        int py = (int)(y * fh / std::max(1, wh));
        int row = k->overlay->rowAt(px, py, fw, fh);
        if (row >= 0) {
            *k->guiRow = row;
            k->guiAdjust(px < fw / 2 ? -1 : 1);
        }
        return;
    }
    if (k->session->wallpaperMode) return;
    double x, y;
    glfwGetCursorPos(win, &x, &y);
    float sx, sy;
    scenePoint(win, x, y, sx, sy);
    if (action == GLFW_PRESS) k->session->proc.pointerDown(sx, sy);
    else k->session->proc.pointerUp(sx, sy);
}

static void onKey(GLFWwindow* win, int key, int, int action, int) {
    Keys* k = (Keys*)glfwGetWindowUserPointer(win);
    if (!k || action != GLFW_PRESS) return;
    if (k->guiOpen && *k->guiOpen && k->guiRow) {
        switch (key) {
            case GLFW_KEY_UP:
                *k->guiRow = (*k->guiRow + kGuiRows - 1) % kGuiRows;
                return;
            case GLFW_KEY_DOWN:
                *k->guiRow = (*k->guiRow + 1) % kGuiRows;
                return;
            case GLFW_KEY_LEFT: k->guiAdjust(-1); return;
            case GLFW_KEY_RIGHT: k->guiAdjust(1); return;
            case GLFW_KEY_SPACE: k->guiPause(); return;
            case GLFW_KEY_TAB: *k->guiOpen = false; return;
            case GLFW_KEY_ESCAPE:
                if (k->guiAlways) glfwSetWindowShouldClose(win, GLFW_TRUE);
                else *k->guiOpen = false;
                return;
            default: return;
        }
    }
    if (key == GLFW_KEY_TAB && k->guiOpen) {
        *k->guiOpen = true;
        return;
    }
    if (key == GLFW_KEY_SPACE) *k->paused = !*k->paused;
    if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(win, GLFW_TRUE);
}

int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&](const char* name) -> std::string {
            if (i + 1 >= argc) { fprintf(stderr, "%s needs a value\n", name); exit(1); }
            return argv[++i];
        };
        if (a == "--help" || a == "-h") { usage(); return 0; }
        else if (a == "--assets") o.assets = val("--assets");
        else if (a == "--shaders") o.shaders = val("--shaders");
        else if (a == "--program") o.program = val("--program");
        else if (a == "--theme") o.theme = val("--theme");
        else if (a == "--appearance") o.appearance = val("--appearance");
        else if (a == "--presentation") o.presentation = val("--presentation");
        else if (a == "--fps") o.fps = std::max(1, atoi(val("--fps").c_str()));
        else if (a == "--monitor") o.monitor = atoi(val("--monitor").c_str());
        else if (a == "--width") o.width = atoi(val("--width").c_str());
        else if (a == "--height") o.height = atoi(val("--height").c_str());
        else if (a == "--speed") o.speed = std::max(0.0f, (float)atof(val("--speed").c_str()));
        else if (a == "--dump-frame") o.dump = val("--dump-frame");
        else if (a == "--dump-mask") o.dumpMask = val("--dump-mask");
        else if (a == "--selftest") o.selftest = true;
        else if (a == "--all-monitors") o.allMonitors = true;
        else if (a == "--layer-shell") o.layerShell = true;
        else if (a == "--layer") o.layer = val("--layer");
        else if (a == "--windowed") o.windowed = true;
        else if (a == "--gui" || a == "--config") o.gui = true;
        else if (a == "--cycle") o.cycle = true;
        else if (a == "--paused") o.paused = true;
        else if (a == "--at") o.at = atof(val("--at").c_str());
        else if (a == "--camera") {
            if (sscanf(val("--camera").c_str(), "%f,%f", &o.camX, &o.camY) != 2) return 1;
            o.hasCam = true;
        } else if (a == "--scroll") {
            if (sscanf(val("--scroll").c_str(), "%f,%f", &o.scrX, &o.scrY) != 2) return 1;
            o.hasScroll = true;
        } else if (a == "--list-programs") o.listOnly = true;
        else if (a == "--list-themes") return listThemes();
        else {
            fprintf(stderr, "unknown option %s (try --help)\n", a.c_str());
            return 1;
        }
    }

    if (o.gui) {
        o.windowed = true;
        if (o.width <= 0) o.width = 1100;
        if (o.height <= 0) o.height = 760;
    }
    o.assets = resolveDir(o.assets, "resources");
    o.shaders = resolveDir(o.shaders, "shaders");
    std::map<std::string, Program> programs;
    loadPrograms(o, programs);
    if (o.listOnly) return listPrograms(programs);
    if (programs.empty()) {
        fprintf(stderr, "no .program files found in %s\n", o.assets.c_str());
        fprintf(stderr, "pass --assets DIR with local wallpaper resources (.program, .png, .bin)\n");
        return 1;
    }

    std::vector<std::string> sequence;
    if (o.cycle) {
        for (auto& kv : programs) sequence.push_back(kv.first);
        std::shuffle(sequence.begin(), sequence.end(), std::mt19937((unsigned)time(nullptr)));
    } else {
        if (!programs.count(o.program)) {
            fprintf(stderr, "unknown program %s (use --list-programs)\n", o.program.c_str());
            return 1;
        }
        sequence.push_back(o.program);
    }

    bool dark = o.appearance == "dark" || (o.appearance == "auto" && systemIsDark());
    bool use24h = true;
    if (const char* fmt = nl_langinfo(T_FMT))
        use24h = strstr(fmt, "%H") != nullptr || strstr(fmt, "%k") != nullptr;

#ifdef HAVE_LAYER_SHELL
    // Explicit flag, or automatic when running on Wayland without a display server.
    bool autoLayerShell = getenv("WAYLAND_DISPLAY") && !getenv("DISPLAY");
    if (!o.selftest && !o.gui && (o.layerShell || autoLayerShell) && o.dumpMask.empty())
        return runLayerShell(o, programs, sequence, dark);
#endif

    glfwSetErrorCallback(glfwError);
    if (!glfwInit()) {
        fprintf(stderr, "glfwInit failed\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, (o.dump.empty() && !o.dumpMask.empty()) || o.dump.empty()
                                     ? GLFW_TRUE
                                     : GLFW_FALSE);

    GLFWmonitor* mon = nullptr;
    if (!o.windowed && !o.allMonitors && o.dump.empty()) {
        int count = 0;
        GLFWmonitor** mons = glfwGetMonitors(&count);
        if (count > 0) mon = mons[std::clamp(o.monitor < 0 ? 0 : o.monitor, 0, count - 1)];
    }
    int winX = 0, winY = 0;
    if (o.allMonitors) {
        int count = 0;
        GLFWmonitor** mons = glfwGetMonitors(&count);
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        for (int i = 0; i < count; i++) {
            int mx, my;
            glfwGetMonitorPos(mons[i], &mx, &my);
            const GLFWvidmode* vm = glfwGetVideoMode(mons[i]);
            if (!vm) continue;
            if (i == 0) { x0 = mx; y0 = my; x1 = mx + vm->width; y1 = my + vm->height; }
            x0 = std::min(x0, mx); y0 = std::min(y0, my);
            x1 = std::max(x1, mx + vm->width); y1 = std::max(y1, my + vm->height);
        }
        winX = x0; winY = y0;
        o.width = x1 - x0; o.height = y1 - y0;
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    }
    int winW = o.width, winH = o.height;
    if (winW <= 0 || winH <= 0) {
        if (mon) {
            const GLFWvidmode* vm = glfwGetVideoMode(mon);
            winW = vm ? vm->width : 1920;
            winH = vm ? vm->height : 1080;
        } else {
            winW = 960;
            winH = 640;
        }
    }
    GLFWwindow* win = glfwCreateWindow(winW, winH, "Macintosh", mon, nullptr);
    if (!win) {
        fprintf(stderr, "cannot create window\n");
        glfwTerminate();
        return 1;
    }
    if (o.allMonitors) glfwSetWindowPos(win, winX, winY);
    glfwMakeContextCurrent(win);
    glfwSwapInterval(0);

    Renderer renderer;
    if (!renderer.init(o.shaders)) return 1;

    Session s;
    s.o = &o;
    s.scene.init(o.assets);
    s.dark = dark;
    s.wallpaperMode = o.presentation != "screensaver";
    s.spectrum = o.theme == "spectrum" || o.theme == "random";
    s.randomTheme = o.theme == "random";
    if (!s.spectrum && !parseScheme(o.theme, s.fixedScheme)) return 1;
    if (s.randomTheme) {
        static const ColorScheme pool[] = {ColorScheme::Green, ColorScheme::Yellow,
                                           ColorScheme::Orange, ColorScheme::Red,
                                           ColorScheme::Purple, ColorScheme::Blue};
        s.fixedScheme = pool[s.rng() % 6];
    }
    s.use24h = use24h;
    s.paused = o.paused;
    s.start(programs, sequence);

    if (o.selftest) {
        int fails = 0;
        auto check = [&](bool ok, const char* msg) {
            printf("%s %s\n", ok ? "PASS" : "FAIL", msg);
            if (!ok) fails++;
        };
        BitmapFont font;
        check(font.load(o.assets + "/Chicago12.bin"), "Chicago12.bin parses");
        check(font.advance('0') > 0 && font.advance(':') > 0, "digit/colon advances");
        check(font.advance(' ') == 4, "space advance fallback");
        check(font.width("0123456789") > 40 && font.width("0123456789") < 90,
              "ten digits fit a plausible width");
        {
            Overlay ov;
            check(ov.init(o.assets + "/Chicago12.bin"), "settings overlay loads its font");
            // 1100x760 -> scale 2, panel origin (70,94), row 0 text starts at y 48.
            check(ov.rowAt(0, 0, 1100, 760) == -1 &&
                      ov.rowAt(110, 200, 1100, 760) == 0,
                  "settings overlay row hit test");
        }

        int parsed = 0;
        for (auto& kv : programs)
            if (!kv.second.code.empty()) parsed++;
        check(parsed == (int)programs.size() && parsed >= 14, "all .program files parse");
        {
            int ignored = 0;
            std::string first;
            for (auto& kv : programs)
                for (auto& line : kv.second.ignored) {
                    if (!ignored) first = kv.first + ": " + line;
                    ignored++;
                }
            check(ignored == 0, "every script line maps to an executed command");
            if (ignored) printf("      first ignored line: %s\n", first.c_str());
        }

        { // audit: every scene, control type, cursor and icon is wired up
            static const char* kTypeNames[] = {
                "icon",       "findericon", "cursor",      "caret",     "text",
                "time",       "date",       "slider",      "button",    "radiobutton",
                "radiogroup", "menu",       "canvas",      "puzzle",    "startupicon",
                "imagebutton", "cursorrect"};
            std::set<std::string> usedPng = {"Slider",   "PuzzleTiles", "MacWriteStyleMenu",
                                             "CancelButton", "StartupIcons", "RadioButton"};
            std::set<int> types;
            int scenes = 0, icons = 0, offscreen = 0, offscreenCursors = 0;
            bool ok = true;
            std::string first;
            auto note = [&](const std::string& where, const std::string& why) {
                if (ok) first = where + ": " + why;
                ok = false;
            };
            auto clip = [](int x, int y, int w, int h) {
                return x < 512 && y < 342 && x + w > 0 && y + h > 0;
            };
            struct Seen {
                ControlDecl c;
                std::string image;
            };
            static const double kCheckAt[] = {0.5, 1, 2, 3, 5, 8, 12, 20, 30, 40, 50};
            for (auto& kv : programs) {
                Processor a;
                a.setProgram(&kv.second);
                Scene cs;
                cs.init(o.assets);
                std::vector<Seen> seen;
                std::set<std::string> keys, images, cursorNames;
                bool rendered = false;
                int nextCheck = 0;
                for (double t = 0; t <= 60.0; t += 1.0 / 30.0) {
                    a.tick(t);
                    if (!a.imageName.empty()) images.insert(a.imageName);
                    if (!a.cursorName.empty()) cursorNames.insert(a.cursorName);
                    for (auto& c : a.controls) {
                        std::string key = std::to_string((int)c.type) + "|" + c.name + "|" +
                                          std::to_string((int)c.x) + "," +
                                          std::to_string((int)c.y);
                        if (keys.insert(key).second) seen.push_back({c, a.imageName});
                    }
                    if (nextCheck < (int)(sizeof kCheckAt / sizeof *kCheckAt) &&
                        t >= kCheckAt[nextCheck]) {
                        std::vector<ControlDecl> snap = a.controls;
                        Scene now;
                        now.init(o.assets);
                        now.compose(a.imageName, snap, "None", t, false);
                        int ink = 0;
                        for (int y = 0; y < 342; y++)
                            for (int x = 0; x < 512; x++) ink += now.mask().at(x, y) != 0;
                        rendered |= ink > 0;

                        // Everything outside the interactive controls must still
                        // be Apple's base art, pixel for pixel.
                        std::vector<ControlDecl> noneCtl;
                        Scene plain;
                        plain.init(o.assets);
                        plain.compose(a.imageName, noneCtl, "None", t, false);
                        std::vector<Box> boxes;
                        for (auto& bc : snap) {
                            Box cb = controlBox(bc);
                            if (bc.type == ControlDecl::Text ||
                                bc.type == ControlDecl::Time ||
                                bc.type == ControlDecl::Date) {
                                // These draw left or right from the anchor and
                                // are not clipped to a fixed frame.
                                cb.x -= 180;
                                cb.w = 360;
                                cb.h = 96;
                            } else if (bc.type == ControlDecl::Cursor) {
                                cb.x -= 16;
                                cb.y -= 16;
                                cb.w = cb.h = 32;
                            }
                            boxes.push_back(cb);
                        }
                        { // the scene cursor follows the scripted pointer
                            Box cb;
                            cb.x = (int)a.mouseX - 16;
                            cb.y = (int)a.mouseY - 16;
                            cb.w = cb.h = 32;
                            boxes.push_back(cb);
                        }
                        int stray = 0;
                        for (int y = 0; y < 342; y++)
                            for (int x = 0; x < 512; x++) {
                                bool interactive = false;
                                for (auto& bb : boxes)
                                    if (x >= bb.x && x < bb.x + bb.w && y >= bb.y &&
                                        y < bb.y + bb.h) {
                                        interactive = true;
                                        break;
                                    }
                                if (!interactive && now.mask().at(x, y) != plain.mask().at(x, y))
                                    stray++;
                            }
                        if (stray) {
                            int fx = -1, fy = -1;
                            for (int y = 0; y < 342 && fx < 0; y++)
                                for (int x = 0; x < 512; x++) {
                                    bool interactive = false;
                                    for (auto& bb : boxes)
                                        if (x >= bb.x && x < bb.x + bb.w && y >= bb.y &&
                                            y < bb.y + bb.h) {
                                            interactive = true;
                                            break;
                                        }
                                    if (!interactive &&
                                        now.mask().at(x, y) != plain.mask().at(x, y)) {
                                        fx = x;
                                        fy = y;
                                        break;
                                    }
                                }
                            printf("      %s t=%.1f stray=%d first=%d,%d\n", kv.first.c_str(),
                                   t, stray, fx, fy);
                            note(kv.first, "draws outside its controls (" +
                                               std::to_string(stray) + " px)");
                        }
                        nextCheck++;
                    }
                }
                if (!rendered) note(kv.first, "renders an empty screen");
                for (auto& img : images) {
                    usedPng.insert(img);
                    if (!fs::exists(o.assets + "/" + img + ".png"))
                        note(kv.first, "base image \"" + img + "\" has no PNG");
                }
                for (auto& cursor : cursorNames)
                    if (!cs.hasCursor(cursor))
                        note(kv.first, "unknown scene cursor \"" + cursor + "\"");

                for (auto& s : seen) {
                    const ControlDecl& c = s.c;
                    types.insert((int)c.type);
                    const char* kind = nullptr;
                    int bx = 0, by = 0, bw = 0, bh = 0, fw = 0, fh = 0;
                    std::string name = c.name;
                    bool additive = false;
                    switch (c.type) {
                        case ControlDecl::Icon:
                            kind = "icon";
                            bx = (int)c.x;
                            by = (int)c.y - 31;
                            bw = fw = 32;
                            bh = fh = 32;
                            additive = true;
                            break;
                        case ControlDecl::FinderIcon:
                            kind = "findericon";
                            bx = (int)(c.x + c.dragX);
                            by = (int)(c.y - 47 + c.dragY);
                            bw = fw = 64;
                            bh = fh = 48;
                            additive = true;
                            break;
                        case ControlDecl::StartupIcon:
                            kind = "startupicon";
                            if (name.empty()) name = "StartupIcons";
                            bx = (int)c.x;
                            by = (int)c.y;
                            bw = fw = 32;
                            bh = fh = 32;
                            additive = true;
                            break;
                        case ControlDecl::ImageButton:
                            kind = "imagebutton";
                            if (name.empty()) name = "CancelButton";
                            bx = (int)c.x;
                            bw = fw = c.w > 0 ? (int)c.w : 74;
                            bh = fh = c.h > 0 ? (int)c.h : 18;
                            by = (int)c.y - bh + 1;
                            break;
                        case ControlDecl::RadioButton:
                            if (name.empty() || name == "None") break;
                            kind = "radiobutton";
                            bx = (int)c.x;
                            by = (int)c.y - 15;
                            bw = fw = 16;
                            bh = fh = 16;
                            break;
                        default:
                            break;
                    }
                    if (kind) {
                        icons++;
                        usedPng.insert(name);
                        if (!cs.hasSprite(name, fw, fh))
                            note(kv.first, std::string(kind) + " \"" + name +
                                               "\" has no " + std::to_string(fw) + "x" +
                                               std::to_string(fh) + " sprite");
                        if (!clip(bx, by, bw, bh)) {
                            offscreen++;
                            continue;
                        }
                        if (additive) {
                            std::vector<ControlDecl> none, one{c};
                            Scene b, f;
                            b.init(o.assets);
                            f.init(o.assets);
                            double when = std::max(0.25, c.startTime + 0.25);
                            b.compose(s.image, none, "None", when, false);
                            f.compose(s.image, one, "None", when, false);
                            int diff = 0;
                            for (int y = std::max(0, by); y < std::min(342, by + bh); y++)
                                for (int x = std::max(0, bx); x < std::min(512, bx + bw); x++)
                                    diff += b.mask().at(x, y) != f.mask().at(x, y);
                            if (!diff)
                                note(kv.first, std::string(kind) + " \"" + name +
                                                   "\" is not drawn in its frame");

                            // Pixel-exact placement: with a single control the
                            // composed frame must be base + that sprite, at the
                            // scripted x/y and with the scripted frame index.
                            const Sprite* sp = cs.getSprite(name, fw, fh);
                            if (sp && sp->frames > 0) {
                                int fr = 0;
                                if (c.type == ControlDecl::Icon)
                                    fr = (int)(((when - c.startTime) * 60.0) /
                                               std::max(1, (int)std::lround(c.value)));
                                else if (c.type == ControlDecl::StartupIcon)
                                    fr = std::fmod(when - c.startTime, 1.5) > 0.75 ? 1 : 0;
                                fr = sp->clampFrame(fr);
                                const uint8_t* ink =
                                    sp->ink.data() + (size_t)fr * fw * fh;
                                const uint8_t* op =
                                    sp->opaque.data() + (size_t)fr * fw * fh;
                                int wrong = 0;
                                for (int y = 0; y < 342; y++)
                                    for (int x = 0; x < 512; x++) {
                                        int sx = x - bx, sy = y - by;
                                        int want = b.mask().at(x, y);
                                        if (sx >= 0 && sx < fw && sy >= 0 && sy < fh) {
                                            int i = sy * fw + sx;
                                            if (c.type == ControlDecl::Icon)
                                                want = want || ink[i];
                                            else if (op[i])
                                                want = ink[i];
                                        }
                                        wrong += f.mask().at(x, y) != want;
                                    }
                                if (wrong)
                                    note(kv.first, std::string(kind) + " \"" + name +
                                                       "\" is not pixel-exact in its frame");
                            }
                        }
                    } else if (c.type == ControlDecl::Cursor ||
                               c.type == ControlDecl::CursorRect) {
                        if (!cs.hasCursor(name))
                            note(kv.first, "unknown cursor \"" + name + "\"");
                        if (c.type == ControlDecl::Cursor) {
                            if (!clip((int)c.x, (int)c.y, 16, 16)) {
                                offscreenCursors++;
                                continue;
                            }
                            std::vector<ControlDecl> none, one{c};
                            Scene b, f;
                            b.init(o.assets);
                            f.init(o.assets);
                            double when = std::max(0.25, c.startTime + 0.25);
                            b.compose(s.image, none, "None", when, false);
                            f.compose(s.image, one, "None", when, false);
                            int bx = (int)c.x - 16, by = (int)c.y - 15 - 16, diff = 0;
                            for (int y = std::max(0, by); y < std::min(342, by + 32); y++)
                                for (int x = std::max(0, bx); x < std::min(512, bx + 32); x++)
                                    diff += b.mask().at(x, y) != f.mask().at(x, y);
                            if (!diff)
                                note(kv.first, "cursor \"" + name + "\" is not drawn");
                        }
                    }
                }
                scenes++;
            }
            // every PNG the extractor installs must be reachable from a script
            for (auto& e : fs::directory_iterator(o.assets)) {
                if (e.path().extension() != ".png") continue;
                std::string stem = e.path().stem().string();
                if (!usedPng.count(stem)) note("assets", "unused " + stem + ".png");
            }
            check(ok, "every scene, icon and cursor is wired up");
            if (!ok) printf("      first problem: %s\n", first.c_str());
            check(types.size() == 17, "all 17 control types occur in the programs");
            if (types.size() != 17) {
                printf("      missing:");
                for (int i = 0; i < 17; i++)
                    if (!types.count(i)) printf(" %s", kTypeNames[i]);
                printf("\n");
            }
            check(offscreen == 0, "every icon/button sits inside the 512x342 screen");
            printf("      %d scenes, %d icon/button controls, %zu PNG assets, "
                   "%d off-screen cursors\n",
                   scenes, icons, usedPng.size(), offscreenCursors);
        }

        { // Camera: the recovered composition is Rx(camera.y) * Rz(camera.x).
          // Emulating 0x100008838-0x100008968 instruction by instruction
          // (sub_10000B480, quat->matrix, lookAt) reproduces these numbers to
          // <1e-9 for every tested angle.
            float b[9];
            cameraBasis(-22.5f, 45.0f, b);
            check(std::fabs(b[0] - 0.923880f) < 1e-4f &&
                      std::fabs(b[1] - 0.382683f) < 1e-4f &&
                      std::fabs(b[3] + 0.270598f) < 1e-4f &&
                      std::fabs(b[5] - 0.707107f) < 1e-4f &&
                      std::fabs(b[8] + 0.707107f) < 1e-4f,
                  "camera rotation is Rx(y)*Rz(x), recovered order and signs");
            cameraBasis(22.5f, 45.0f, b);
            check(std::fabs(b[1] + 0.382683f) < 1e-4f &&
                      std::fabs(b[3] - 0.270598f) < 1e-4f,
                  "camera roll sign follows camera.x");
            cameraBasis(0.0f, 0.0f, b);
            check(std::fabs(b[0] - 1.0f) < 1e-6f && std::fabs(b[4] - 1.0f) < 1e-6f &&
                      std::fabs(b[8] + 1.0f) < 1e-6f && std::fabs(b[1]) < 1e-6f,
                  "camera at 0,0 is the lookAt z reflection");

            // Full 2D VP (projection * camera) at the 4:3 selftest size.
            cameraScreenBasis(-22.5f, 45.0f, 640.0f / 480.0f, b);
            check(std::fabs(b[0] + 0.013198f) < 1e-5f &&
                      std::fabs(b[1] + 0.005467f) < 1e-5f &&
                      std::fabs(b[2] + 0.005154f) < 1e-5f &&
                      std::fabs(b[3] - 0.012443f) < 1e-5f,
                  "camera+projection 2x2 is numerically pinned");
            cameraScreenBasis(22.5f, 45.0f, 640.0f / 480.0f, b);
            check(std::fabs(b[0] + 0.013198f) < 1e-5f &&
                      std::fabs(b[1] - 0.005467f) < 1e-5f &&
                      std::fabs(b[2] - 0.005154f) < 1e-5f &&
                      std::fabs(b[3] - 0.012443f) < 1e-5f,
                  "camera+projection matches the exact block for +22.5 too");

            // v93/v94 fit: flat camera is ~1, rolled 16:9 needs >1.2 so the
            // visible preimage stays inside the fov*fov instance grid.
            float flat = cameraFitScale(0.0f, 0.0f, 16.0f / 9.0f);
            float rolled = cameraFitScale(-22.5f, 45.0f, 16.0f / 9.0f);
            check(flat >= 1.0f && flat < 1.02f && rolled > 1.15f,
                  "camera fit keeps the rolled screen inside the fov^2 grid");
        }

        Color green = schemeColor(ColorScheme::Green);
        check(std::fabs(green.r - srgbToLinear(0.388235f)) < 1e-4f &&
                  std::fabs(green.g - srgbToLinear(0.854902f)) < 1e-4f &&
                  std::fabs(green.b - srgbToLinear(0.219608f)) < 1e-4f,
              "system green is macOS 27 AppKit #63DA38");
        Color greenDark = schemeColor(ColorScheme::Green, true);
        check(std::fabs(greenDark.r - green.r) < 1e-6f &&
                  std::fabs(greenDark.g - green.g) < 1e-6f &&
                  std::fabs(greenDark.b - green.b) < 1e-6f,
              "no separate dark scheme table (sub_1000139CC has no appearance)");
        Shading light = makeShading(green, false, 0.666f);
        check(light.ink.g < light.bgBottom.g && light.bgTop.g > light.bgBottom.g,
              "ink darker, gradient top lighter");
        check(std::abs(light.ink.g / light.bgBottom.g - 0.666f) < 0.01f,
              "wallpaper contrast is 0.666");
        Shading screensaver = makeShading(green, false, 0.2f);
        check(screensaver.ink.g < light.ink.g, "screensaver contrast is lower");
        Shading darkS = makeShading(green, true, 0.666f);
        check(darkS.bgBottom.g < light.bgBottom.g, "dark appearance dims the paper");

        Color c0 = spectrumColor(0.0), c30 = spectrumColor(30.0), c50 = spectrumColor(50.0);
        check(std::abs(c0.g - green.g) < 1e-4f && c30.r > c0.r, "spectrum cycles over 60 s");
        check(c50.b > 0.5f && c50.r < 0.2f, "spectrum reaches blue before wrapping");

        { // the control file and --gui share these switches
            Options top = o;
            Session t;
            t.o = &top;
            t.spectrum = true;
            check(setTheme(t, "red") && !t.spectrum &&
                      t.fixedScheme == ColorScheme::Red,
                  "live theme switch selects red");
            check(setTheme(t, "spectrum") && t.spectrum,
                  "live theme switch selects spectrum");
            check(!setTheme(t, "chartreuse"), "unknown live theme is rejected");
            check(applySetting(t, programs, "fps", "60") && top.fps == 60,
                  "live fps applies");
            check(applySetting(t, programs, "paused", "on") && t.paused,
                  "live pause applies");
            check(applySetting(t, programs, "program", "MacWrite") &&
                      t.sequence.size() == 1 && t.sequence[0] == "MacWrite" && !top.cycle,
                  "live program switch selects MacWrite");
            check(!applySetting(t, programs, "program", "NoSuchProgram"),
                  "unknown live program is rejected");
        }

        Processor p;
        p.setProgram(&programs["IconGardenWallpaper"]);
        p.tick(0.0);
        check(p.scale >= 0.99f, "scale in (instant) resolves to 1");
        check(p.zoom == 0.0f, "default zoom is neutral (factor 1, not 3)");
        check(!p.controls.empty(), "IconGardenWallpaper adds controls");
        check(p.imageName == "IconGarden", "image name bound");
        s.scene.compose(p.imageName, p.controls, p.cursorName, 0.0, false, 0.5333);
        check(s.scene.mask().w == 512 && s.scene.mask().h == 342, "scene mask is 512x342");

        { // Cursor atlas: arrow/text/crosshair are CURS_0..2; watch/ball/hand/
          // Clarus come from acur_* frame lists and advance over time.
            Scene cs;
            cs.init(o.assets);
            std::vector<ControlDecl> none;
            auto draw = [&](const char* name, double t) {
                cs.compose("", none, name, t, false, 0.5333, 100, 100);
                int n = 0;
                long h = 0;
                const Bitmap& b = cs.mask();
                for (int y = 80; y < 120; y++)
                    for (int x = 80; x < 120; x++)
                        if (b.at(x, y)) {
                            n++;
                            h += 1 + x * 3 + y * 7;
                        }
                return std::pair<int, long>{n, h};
            };
            auto text = draw("Text", 0.0);
            auto arrow = draw("Arrow", 0.0);
            auto cross = draw("Crosshair", 0.0);
            check(text.first > 0 && text.first < 40, "Text cursor is the I-beam, not a dog");
            check(arrow.first > 40 && arrow.first < 70, "Arrow cursor is the pointer");
            check(cross.first > 0 && cross.first < 40 && cross.second != text.second,
                  "Crosshair cursor is the plus, not the I-beam");
            check(draw("Watch", 0.0).second != draw("Watch", 0.4).second,
                  "Watch cursor animates");
            check(draw("Ball", 0.0).second != draw("Ball", 0.4).second,
                  "Ball cursor animates");
            check(draw("Hand", 0.0).second != draw("Hand", 0.4).second,
                  "Hand cursor animates");
            check(draw("Clarus", 0.0).second != draw("Clarus", 1.0).second,
                  "Clarus cursor animates");
            auto idsAre = [&](const char* name, const std::vector<int>& want) {
                const std::vector<int>* ids = cs.animatedIds(name);
                return ids && *ids == want;
            };
            check(idsAre("Watch", {4, 2400, 2401, 2402, 2403, 2404, 2405, 2406}),
                  "Watch frames are CURS_4 + 2400…2406");
            check(idsAre("Hand", {-20714, -20713, -20712, -20711, -20710, -20709}),
                  "Hand frames are CURS_-20714…-20709");
            check(idsAre("Ball", {25001, 25005, 25002, 25006, 25003, 25007, 25004, 25008}),
                  "Ball frames are CURS_25001…25008");
            {
                const std::vector<int>* ids = cs.animatedIds("Clarus");
                check(ids && ids->size() == 30 &&
                          std::all_of(ids->begin(), ids->end(),
                                      [](int id) { return id >= 128 && id <= 136; }),
                      "Clarus frames are the 30 acur_640 dog poses");
            }
        }

        { // Text mouse selection (MacWriteTextInput drags over its text field).
            Processor q;
            q.setProgram(&programs["MacWriteTextInput"]);
            q.tick(0.0);
            ControlDecl* t = nullptr;
            for (auto& c : q.controls)
                if (c.type == ControlDecl::Text) t = &c;
            check(t != nullptr, "MacWriteTextInput adds a text field");
            if (t) {
                auto ink = [&](int x0, int y0, int x1, int y1) {
                    long n = 0;
                    const Bitmap& m = s.scene.mask();
                    for (int y = y0; y < y1; y++)
                        for (int x = x0; x < x1; x++)
                            n += (m.at(x, y) != 0) * (1 + x + y * 3);
                    return n;
                };
                t->text = "Hello";
                s.scene.compose(q.imageName, q.controls, q.cursorName, 0.0, false, 0.5333,
                                0, 0, false);
                int before = ink(13, 90, 45, 101);
                s.scene.compose(q.imageName, q.controls, q.cursorName, 0.1, false, 0.5333,
                                14, 94, true);
                s.scene.compose(q.imageName, q.controls, q.cursorName, 0.2, false, 0.5333,
                                27, 94, true);
                check(t->selStart == 0 && t->selEnd >= 2, "text drag makes a selection");
                check(ink(13, 90, 45, 101) != before, "selection inverts the text line");
                s.scene.compose(q.imageName, q.controls, q.cursorName, 0.3, false, 0.5333,
                                0, 0, false);
            }
        }

        { // Widget state and the pointer router.
            Processor q;
            q.setProgram(&programs["System6ControlPanel"]);
            q.tick(0.0);
            ControlDecl* slider = nullptr;
            ControlDecl* radio = nullptr;
            for (auto& c : q.controls) {
                if (c.type == ControlDecl::Slider) slider = &c;
                if (c.type == ControlDecl::RadioButton && !radio) radio = &c;
            }
            check(slider && radio, "System6 adds slider and radio controls");
            if (slider && radio) {
                Box b = controlBox(*slider);
                double before = slider->offset;
                q.pointerDown(b.x + 5, b.y + 5);
                q.pointerMove(b.x + 5, b.y - 19);
                q.pointerUp(b.x + 5, b.y - 19);
                check(before == 24 && slider->offset == 48,
                      "slider drag tracks the pointer and snaps to 12 px");
                Box r = controlBox(*radio);
                q.pointerClick(r.x + 8, r.y + 8);
                check(radio->selected && !radio->highlighted, "radio click selects it");
                bool others = false;
                for (auto& c : q.controls)
                    if (c.type == ControlDecl::RadioButton && &c != radio &&
                        c.group == radio->group && c.selected)
                        others = true;
                check(!others, "radio group keeps a single selection");
            }
        }
        {
            Processor q;
            q.setProgram(&programs["DeskAccessories"]);
            q.tick(0.0);
            ControlDecl* puzzle = nullptr;
            ControlDecl* button = nullptr;
            for (auto& c : q.controls) {
                if (c.type == ControlDecl::Puzzle) puzzle = &c;
                if (c.type == ControlDecl::Button) button = &c;
            }
            check(puzzle != nullptr, "DeskAccessories adds the puzzle");
            if (puzzle) {
                Box b = controlBox(*puzzle);
                q.pointerClick(b.x + 10, b.y + 30); // cell 4, beside the blank at cell 5
                check(puzzle->tiles[4] == 0 && puzzle->tiles[5] == 11,
                      "puzzle slides the line into the blank");
            }
            if (button) {
                Box b = controlBox(*button);
                q.pointerClick(b.x + 2, b.y + 2);
                check(!q.firedActions.empty(), "calculator button fires its action");
            }
        }
        {
            Processor q;
            q.setProgram(&programs["MacWriteStyleMenu"]);
            q.tick(0.0);
            ControlDecl* menu = nullptr;
            for (auto& c : q.controls)
                if (c.type == ControlDecl::Menu) menu = &c;
            check(menu != nullptr, "MacWrite adds the menu");
            if (menu) {
                q.pointerDown(menu->x + 20, 30);
                check(menu->mouseDown && menu->hilitedRow == 19, "menu opens on press and hilites");
                q.pointerUp(menu->x + 20, 30);
                q.tick(0.5);
                check(!menu->mouseDown || menu->flashStart >= 0, "menu closes with a flash");
            }
        }
        {
            Processor q;
            q.setProgram(&programs["StartupHappyMac"]);
            q.tick(0.0);
            ControlDecl* icon = nullptr;
            for (auto& c : q.controls)
                if (c.type == ControlDecl::StartupIcon) icon = &c;
            check(icon != nullptr, "startup adds the icon");
            if (icon) {
                q.pointerClick(256, 171);
                check(icon->happy, "clicking the startup icon shows the Happy Mac");
            }
        }
        {
            Processor q;
            q.setProgram(&programs["MacPaintHello"]);
            q.tick(0.0);
            ControlDecl* canvas = nullptr;
            for (auto& c : q.controls)
                if (c.type == ControlDecl::Canvas) canvas = &c;
            check(canvas != nullptr, "MacPaintHello adds the canvas");
            if (canvas) {
                Box b = controlBox(*canvas);
                q.pointerDown(b.x + 10, b.y + 10);
                q.pointerMove(b.x + 20, b.y + 15);
                q.pointerMove(b.x + 30, b.y + 12);
                q.pointerUp(b.x + 30, b.y + 12);
                check(canvas->strokes.size() >= 12, "canvas records the pen path");
                std::vector<ControlDecl> none;
                Scene plain, drawn;
                plain.init(o.assets);
                drawn.init(o.assets);
                plain.compose(q.imageName, none, "None", 0.0, false);
                drawn.compose(q.imageName, q.controls, "None", 0.0, false);
                int ink = 0;
                for (int y = b.y; y < b.y + b.h; y++)
                    for (int x = b.x; x < b.x + b.w; x++)
                        ink += plain.mask().at(x, y) != drawn.mask().at(x, y);
                check(ink > 0, "canvas strokes reach the composed frame");
            }
        }
        { // Finder drags the folder icon onto the trash icon.
            Processor q;
            q.setProgram(&programs["Finder"]);
            for (double t = 0; t <= 45.0; t += 1.0 / 60.0) q.tick(t);
            bool dragged = false, trashed = false;
            for (auto& c : q.controls)
                if (c.type == ControlDecl::FinderIcon) {
                    if (c.name.find("Folder") != std::string::npos)
                        trashed = c.trashed;
                    if (c.tracking) dragged = true;
                }
            check(dragged || trashed, "Finder drags its folder icon");
            check(trashed, "dropping the folder on the trash removes it");
        }
        { // cursorrect: the pointer over the control panel shows the crosshair.
            Processor q;
            q.setProgram(&programs["System6ControlPanel"]);
            q.tick(0.0);
            auto countDiff = [&](const Bitmap& m, const Bitmap& ref, int cx, int cy) {
                int n = 0;
                for (int y = std::max(0, cy - 16); y < std::min(342, cy + 16); y++)
                    for (int x = std::max(0, cx - 16); x < std::min(512, cx + 16); x++)
                        n += m.at(x, y) != ref.at(x, y);
                return n;
            };
            std::vector<ControlDecl> none;
            Scene plain, inside, outside;
            plain.init(o.assets);
            inside.init(o.assets);
            outside.init(o.assets);
            plain.compose(q.imageName, none, "None", 0.0, false, 0.5333, 0, 0, false);
            inside.compose(q.imageName, q.controls, q.cursorName, 0.0, false, 0.5333, 250,
                           200, false);
            outside.compose(q.imageName, q.controls, q.cursorName, 0.0, false, 0.5333, 60, 60,
                            false);
            check(countDiff(inside.mask(), plain.mask(), 250, 200) > 0,
                  "cursorrect shows the crosshair inside its frame");
            check(countDiff(outside.mask(), plain.mask(), 60, 60) == 0,
                  "cursorrect leaves the cursor alone outside its frame");
        }
        { // The caret blinks on the recovered 0.533 s interval.
            Processor q;
            q.setProgram(&programs["System1ControlPanel"]);
            q.tick(0.0);
            Scene a, b;
            a.init(o.assets);
            b.init(o.assets);
            a.compose(q.imageName, q.controls, "None", 0.0, false);
            b.compose(q.imageName, q.controls, "None", 0.6, false);
            int on = 0, off = 0;
            for (int y = 147; y < 165; y++) {
                on += a.mask().at(381, y) != 0;
                off += b.mask().at(381, y) != 0;
            }
            check(on > off, "caret blinks");
        }
        { // The keyboard instruction fills the MacWrite text field.
            Processor q;
            q.setProgram(&programs["MacWriteTextInput"]);
            for (double t = 0; t <= 10.0; t += 1.0 / 60.0) q.tick(t);
            const ControlDecl* t = nullptr;
            for (auto& c : q.controls)
                if (c.type == ControlDecl::Text) t = &c;
            check(t && t->text.find("crazy ones") != std::string::npos,
                  "keyboard types the MacWrite text");
        }

        // Palette-independent test colours, so a system-colour change cannot
        // invalidate the geometric/ink checks below.
        Shading testSh;
        testSh.ink = {0, 0, 0, 1};
        testSh.bgTop = {1, 1, 1, 1};
        testSh.bgBottom = {1, 1, 1, 1};

        { // The recovered pixel quad is +-0.45, i.e. 0.9 of the texel pitch
          // (the shadow is +-1.125). A regression to half-size quads shows up
          // as a much thinner single-pixel mark.
            Bitmap one;
            one.reset(512, 342);
            one.at(256, 171) = 1;
            renderer.setSize(640, 480);
            renderer.draw(one, testSh, 0.0f, 0.0f, 256, 171, 1.0f, 0.0f, "None", 0.0f);
            std::vector<uint8_t> px;
            renderer.readRgb(px);
            int dark = 0;
            for (int y = 220; y < 260; y++)
                for (int x = 300; x < 340; x++)
                    dark += px[((size_t)y * 640 + x) * 3 + 1] < 170;
            check(dark >= 8, "pixel quad covers 0.9 of the cell pitch");

            // The exact 0x100008838–0x1000089a8 VP puts the shadow above the
            // texel at camera (-22.5, 45). Measure the shadow centroid.
            renderer.draw(one, testSh, -22.5f, 45.0f, 256, 171, 1.0f, 0.0f, "None", 0.0f);
            renderer.readRgb(px);
            double inkY = 0, inkN = 0, shY = 0, shW = 0;
            for (int y = 210; y < 270; y++)
                for (int x = 290; x < 350; x++) {
                    int g = px[((size_t)y * 640 + x) * 3 + 1];
                    if (g < 20) {
                        inkY += y;
                        inkN += 1;
                    } else if (g < 250) {
                        shY += y * (255 - g);
                        shW += 255 - g;
                    }
                }
            check(inkN > 0 && shW > 0 && shY / shW > inkY / inkN,
                  "shadow offset matches the exact 27 VP");
        }

        s.scene.compose(p.imageName, p.controls, p.cursorName, 0.0, false, 0.5333);
        renderer.setSize(640, 480);
        renderer.draw(s.scene.mask(), testSh, 0.0f, 0.0f, p.scrollX, p.scrollY, p.scale,
                      p.zoom, p.rampName, 0.0f);
        std::vector<uint8_t> rgb;
        renderer.readRgb(rgb);
        int lo = 0, hi = 0;
        for (size_t i = 0; i < rgb.size(); i += 3) {
            if (rgb[i + 1] < 170) lo++;
            if (rgb[i + 1] > 200) hi++;
        }
        check(lo > 500 && hi > 500, "rendered frame contains ink and paper");

        { // The final pass must dither: a nearly flat paper gradient would
          // otherwise quantise into tall 8-bit stripes (10-84 px on screen).
            Bitmap paper;
            paper.reset(500, 340);
            Shading flat;
            flat.ink = {0, 0, 0, 1};
            flat.bgTop = {0.60f, 0.80f, 0.50f, 1};
            flat.bgBottom = {0.55f, 0.78f, 0.45f, 1};
            renderer.setSize(320, 240);
            renderer.draw(paper, flat, 0.0f, 0.0f, 250, 170, 1.0f, 0.0f, "None", 0.0f);
            std::vector<uint8_t> px;
            renderer.readRgb(px);
            // A dithered row mean moves by a fraction of a code value every
            // row; without dither it sits on one level for tens of rows.
            int distinct = 0;
            double prev = -1;
            for (int y = 0; y < 240; y++) {
                double m = 0;
                for (int x = 20; x < 300; x++) m += px[((size_t)y * 320 + x) * 3 + 1];
                m /= 280.0;
                if (std::fabs(m - prev) > 0.002) { distinct++; prev = m; }
            }
            check(distinct > 30, "8-bit output is dithered (no gradient bands)");
        }
        renderer.shutdown();
        glfwDestroyWindow(win);
        glfwTerminate();
        printf("%s: %d failure(s)\n", fails ? "SELFTEST FAILED" : "SELFTEST OK", fails);
        return fails ? 1 : 0;
    }

    // ---- settings overlay (GLFW): live program/theme/appearance/presentation ----
    Overlay overlay;
    if (!overlay.init(o.assets + "/Chicago12.bin")) {
        fprintf(stderr, "cannot load %s/Chicago12.bin\n", o.assets.c_str());
        return 1;
    }
    static const char* kThemes[] = {"green", "yellow", "orange", "red",   "purple",
                                    "blue",  "gray",   "darkGray", "spectrum", "random"};
    static const char* kAppear[] = {"light", "dark", "auto"};
    static const char* kPres[] = {"wallpaper", "screensaver"};
    std::vector<std::string> progNames;
    for (auto& kv : programs) progNames.push_back(kv.first);
    progNames.push_back("cycle");
    int progIdx = 0, themeIdx = 8, appIdx = 2, presIdx = o.presentation == "screensaver" ? 1 : 0;
    for (int i = 0; i < (int)progNames.size(); i++)
        if (progNames[i] == (o.cycle ? "cycle" : o.program)) progIdx = i;
    for (int i = 0; i < (int)(sizeof kThemes / sizeof *kThemes); i++)
        if (o.theme == kThemes[i]) themeIdx = i;
    for (int i = 0; i < 3; i++)
        if (o.appearance == kAppear[i]) appIdx = i;

    auto startProgram = [&](const std::string& name) {
        o.program = name;
        o.cycle = name == "cycle";
        if (o.cycle) {
            std::vector<std::string> all;
            for (auto& kv : programs) all.push_back(kv.first);
            s.start(programs, all);
        } else {
            s.start(programs, {name});
        }
        s.seqIndex = 0;
        s.nextProgramAt = -1;
        s.timeModeFromControls = false;
        s.paused = false; // picking a program means "show it"
    };
    auto applyTheme = [&](const std::string& name) { setTheme(s, name); };
    auto wrap = [](int v, int n) { return (v % n + n) % n; };
    bool guiOpen = o.gui;
    int guiRow = 0;
    auto guiAdjust = [&](int d) {
        switch (guiRow) {
            case 0:
                progIdx = wrap(progIdx + d, (int)progNames.size());
                startProgram(progNames[progIdx]);
                break;
            case 1:
                themeIdx = wrap(themeIdx + d, (int)(sizeof kThemes / sizeof *kThemes));
                applyTheme(kThemes[themeIdx]);
                break;
            case 2:
                appIdx = wrap(appIdx + d, 3);
                o.appearance = kAppear[appIdx];
                s.dark = o.appearance == "dark" ||
                         (o.appearance == "auto" && systemIsDark());
                break;
            case 3:
                presIdx = wrap(presIdx + d, 2);
                o.presentation = kPres[presIdx];
                s.wallpaperMode = o.presentation != "screensaver";
                break;
            case 4:
                o.speed = std::clamp(std::round((o.speed + 0.25f * d) * 4.0f) / 4.0f,
                                     0.25f, 4.0f);
                break;
            case 5:
                o.fps = std::clamp(o.fps + 5 * d, 5, 120);
                break;
            case 6:
                s.paused = !s.paused;
                break;
        }
    };
    auto buildModel = [&]() {
        GuiModel m;
        m.row = guiRow;
        m.program = progNames[progIdx];
        m.theme = kThemes[themeIdx];
        m.appearance = kAppear[appIdx];
        m.presentation = kPres[presIdx];
        m.speed = o.speed;
        m.fps = o.fps;
        m.paused = s.paused;
        return m;
    };

    Keys keys{&s.paused, &s};
    keys.overlay = &overlay;
    keys.guiOpen = &guiOpen;
    keys.guiRow = &guiRow;
    keys.guiAlways = o.gui;
    keys.guiAdjust = guiAdjust;
    keys.guiPause = [&] { s.paused = !s.paused; };
    glfwSetWindowUserPointer(win, &keys);
    glfwSetKeyCallback(win, onKey);
    glfwSetCursorPosCallback(win, onCursorPos);
    glfwSetMouseButtonCallback(win, onMouseButton);

    double t0 = glfwGetTime();
    double last = 0, lastDraw = 0;
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        double now = glfwGetTime() - t0;
        double dt = now - last;
        last = now;
        s.update(dt);

        if (!o.dumpMask.empty()) {
            const Bitmap& m = s.scene.mask();
            std::vector<uint8_t> rgb((size_t)m.w * m.h * 3);
            for (size_t i = 0; i < m.px.size(); i++) {
                uint8_t v = m.px[i] ? 0 : 255;
                rgb[i * 3] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = v;
            }
            writePng(o.dumpMask, m.w, m.h, rgb.data());
            printf("wrote %s (%dx%d)\n", o.dumpMask.c_str(), m.w, m.h);
            break;
        }

        if (now - lastDraw >= 1.0 / o.fps || !o.dump.empty()) {
            lastDraw = now;
            int fbw, fbh;
            glfwGetFramebufferSize(win, &fbw, &fbh);
            s.draw(renderer, fbw, fbh);
            if (guiOpen) overlay.draw(renderer, buildModel(), fbw, fbh);
            glfwSwapBuffers(win);
            if (!o.dump.empty()) {
                std::vector<uint8_t> rgb;
                renderer.readRgb(rgb);
                if (writePng(o.dump, renderer.width(), renderer.height(), rgb.data()))
                    printf("wrote %s (%dx%d)\n", o.dump.c_str(), renderer.width(),
                           renderer.height());
                break;
            }
        }

        if (o.dump.empty()) {
            double spare = 1.0 / o.fps - (glfwGetTime() - t0 - now);
            if (spare > 0.001) {
                struct timespec ts{0, (long)(spare * 1e9)};
                nanosleep(&ts, nullptr);
            }
        }
    }

    renderer.shutdown();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
