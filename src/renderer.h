#pragma once
#include "image.h"
#include "theme.h"
#include <string>
#include <vector>

// Row-major upper-left 3x3 of the recovered camera rotation, for the audit.
void cameraBasis(float camX, float camY, float out[9]);
// Row-major 2x2 of the full recovered VP at zoom 0 (NDC per world xy), for the audit.
void cameraScreenBasis(float camX, float camY, float aspect, float out[4]);
// v93/v94 fit that keeps the rotated screen inside the fov*fov instance grid.
float cameraFitScale(float camX, float camY, float aspect);

class Renderer {
public:
    ~Renderer() = default;
    bool init(const std::string& shaderDir);
    void shutdown();
    void setSize(int w, int h);

    // mask: composed 1-bit screen. camera/scroll/scale/zoom come from the .program VM.
    void draw(const Bitmap& mask, const Shading& shading, float cameraX, float cameraY,
              float scrollX, float scrollY, float animScale, float zoom,
              const std::string& rampName, float speed);

    // Screen-space RGBA panel (settings overlay), centred and nearest-scaled.
    void drawOverlay(const uint8_t* rgba, int w, int h, int scale);

    void readRgb(std::vector<uint8_t>& out);

    int width() const { return w_; }
    int height() const { return h_; }

private:
    struct Program {
        unsigned id = 0;
        void use() const;
        int loc(const char* name) const;
    };
    bool build(Program& p, const std::string& vs, const std::string& fs);
    unsigned compile(unsigned type, const std::string& src, const std::string& label);
    void uploadMask(const Bitmap& b);
    // Linear RGBA16F scene target + the final sRGB/dither pass. This is the
    // compositor half of the pipeline: blending stays linear, 8-bit output is
    // dithered once (see shaders/present.frag).
    bool ensureTarget();
    void present();

    Program pixel_, shadow_, background_, overlay_, present_;
    unsigned fbo_ = 0, fbTex_ = 0;
    int fbW_ = 0, fbH_ = 0;
    bool havePresent_ = false;
    unsigned vao_ = 0, vbo_ = 0;
    unsigned bgVao_ = 0, bgVbo_ = 0;
    unsigned oTex_ = 0;
    int oTexW_ = 0, oTexH_ = 0;
    unsigned tex_ = 0;
    int texW_ = 0, texH_ = 0;
    std::vector<uint8_t> r8_;
    int w_ = 1, h_ = 1;
    bool encode_ = false; // true when the framebuffer is not sRGB-capable
};
