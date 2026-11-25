#include "renderer.h"
#include <epoxy/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {
struct M4 {
    float m[16]; // row-major
};
M4 identity() {
    M4 r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1;
    return r;
}
M4 mul(const M4& a, const M4& b) {
    M4 r{};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[i * 4 + k] * b.m[k * 4 + j];
            r.m[i * 4 + j] = s;
        }
    return r;
}
M4 rotX(float deg) {
    float a = deg * 3.14159265358979f / 180.0f, c = std::cos(a), s = std::sin(a);
    M4 r = identity();
    r.m[5] = c; r.m[6] = -s;
    r.m[9] = s; r.m[10] = c;
    return r;
}
M4 rotZ(float deg) {
    float a = deg * 3.14159265358979f / 180.0f, c = std::cos(a), s = std::sin(a);
    M4 r = identity();
    r.m[0] = c; r.m[1] = -s;
    r.m[4] = s; r.m[5] = c;
    return r;
}

std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
} // namespace

void Renderer::Program::use() const { glUseProgram(id); }
int Renderer::Program::loc(const char* n) const { return glGetUniformLocation(id, n); }

void Renderer::shutdown() {
    if (tex_) glDeleteTextures(1, &tex_);
    if (oTex_) glDeleteTextures(1, &oTex_);
    if (fbTex_) glDeleteTextures(1, &fbTex_);
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (bgVbo_) glDeleteBuffers(1, &bgVbo_);
    if (bgVao_) glDeleteVertexArrays(1, &bgVao_);
    if (pixel_.id) glDeleteProgram(pixel_.id);
    if (shadow_.id) glDeleteProgram(shadow_.id);
    if (background_.id) glDeleteProgram(background_.id);
    if (overlay_.id) glDeleteProgram(overlay_.id);
    if (present_.id) glDeleteProgram(present_.id);
    tex_ = oTex_ = vbo_ = vao_ = bgVbo_ = bgVao_ = 0;
    fbo_ = fbTex_ = 0;
    fbW_ = fbH_ = 0;
    havePresent_ = false;
    oTexW_ = oTexH_ = 0;
    pixel_.id = shadow_.id = background_.id = overlay_.id = present_.id = 0;
}

unsigned Renderer::compile(unsigned type, const std::string& src, const std::string& label) {
    unsigned s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        fprintf(stderr, "shader %s: %s\n", label.c_str(), log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool Renderer::build(Program& p, const std::string& vs, const std::string& fs) {
    unsigned v = compile(GL_VERTEX_SHADER, vs, "vertex");
    unsigned f = compile(GL_FRAGMENT_SHADER, fs, "fragment");
    if (!v || !f) return false;
    p.id = glCreateProgram();
    glAttachShader(p.id, v);
    glAttachShader(p.id, f);
    glLinkProgram(p.id);
    int ok = 0;
    glGetProgramiv(p.id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p.id, sizeof log, nullptr, log);
        fprintf(stderr, "link: %s\n", log);
        return false;
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return true;
}

bool Renderer::init(const std::string& shaderDir) {
    if (getenv("MW_DEBUG")) {
        const GLubyte* name = glGetString(GL_RENDERER);
        const GLubyte* version = glGetString(GL_VERSION);
        fprintf(stderr, "gl renderer: %s | %s\n", name ? (const char*)name : "?",
                version ? (const char*)version : "?");
    }
    std::string dir = shaderDir;
    if (!dir.empty() && dir.back() != '/') dir.push_back('/');
    std::string pv = readFile(dir + "pixel.vert"), pf = readFile(dir + "pixel.frag");
    std::string sf = readFile(dir + "shadow.frag");
    std::string bv = readFile(dir + "background.vert"), bf = readFile(dir + "background.frag");
    std::string ov = readFile(dir + "overlay.vert"), of = readFile(dir + "overlay.frag");
    std::string tv = readFile(dir + "present.vert"), tf = readFile(dir + "present.frag");
    if (pv.empty() || pf.empty() || sf.empty() || bv.empty() || bf.empty()) {
        fprintf(stderr, "cannot read shaders from %s\n", dir.c_str());
        return false;
    }
    if (!build(pixel_, pv, pf)) return false;
    if (!build(shadow_, pv, sf)) return false;
    if (!build(background_, bv, bf)) return false;
    // Without the present pass the renderer falls back to encoding per pixel,
    // which is correct but keeps the 8-bit banding.
    havePresent_ = !tv.empty() && !tf.empty() && build(present_, tv, tf);
    // The overlay is optional: a missing pair only disables the settings panel.
    if (!ov.empty() && !of.empty()) build(overlay_, ov, of);

    // sRGB-capable default framebuffer? If yes the hardware does the encoding and
    // blending happens in linear space, exactly like the Metal layer's linear colorspace.
    GLint enc = GL_LINEAR;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_BACK_LEFT,
                                          GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING, &enc);
    if (enc == GL_SRGB) {
        glEnable(GL_FRAMEBUFFER_SRGB);
        encode_ = false;
    } else {
        encode_ = true;
    }

    // Unit corners: the recovered vertex buffers are +-0.45 (pixel) and
    // +-1.125 (shadow), i.e. uHalf itself is the corner, not half of it.
    static const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    static const float bquad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glGenVertexArrays(1, &bgVao_);
    glBindVertexArray(bgVao_);
    glGenBuffers(1, &bgVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, bgVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof bquad, bquad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);

    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return true;
}

void Renderer::setSize(int w, int h) {
    w_ = w > 0 ? w : 1;
    h_ = h > 0 ? h : 1;
}

void Renderer::uploadMask(const Bitmap& b) {
    if (b.w != texW_ || b.h != texH_) {
        glBindTexture(GL_TEXTURE_2D, tex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, b.w, b.h, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        texW_ = b.w;
        texH_ = b.h;
        r8_.resize((size_t)b.w * b.h);
    }
    for (size_t i = 0; i < b.px.size(); i++) r8_[i] = b.px[i] ? 0 : 255;
    glBindTexture(GL_TEXTURE_2D, tex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, b.w, b.h, GL_RED, GL_UNSIGNED_BYTE, r8_.data());
}

static void rampVector(const std::string& name, float& x, float& y) {
    x = y = 0;
    if (name == "Right") x = 1;
    else if (name == "Left") x = -1;
    else if (name == "Down") y = -1;
    else if (name == "Up") y = 1;
}

static void uploadMat4(GLint loc, const M4& m) {
    float cm[16];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) cm[j * 4 + i] = m.m[i * 4 + j];
    glUniformMatrix4fv(loc, 1, GL_FALSE, cm);
}

// Camera composition from sub_100008420: q1 = Rz(camera.x), then a second
// quaternion around the camera's local X axis by camera.y, and q = q2 * q1.
// In column-vector form that is R = Rx(camera.y) * Rz(camera.x). The exact
// 0x100008838–0x1000089a8 pipeline then applies the lookAt's z reflection,
// which flips the z column; this is what
// moves the shadow to the same side as the shipping renderer. The x flip of
// the same lookAt is folded into P.m[0] below.
static M4 cameraMatrix(float camX, float camY) {
    M4 r = mul(rotX(camY), rotZ(camX));
    r.m[2] = -r.m[2];
    r.m[6] = -r.m[6];
    r.m[10] = -r.m[10];
    return r;
}

void cameraBasis(float camX, float camY, float out[9]) {
    M4 r = cameraMatrix(camX, camY);
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) out[i * 3 + j] = r.m[i * 4 + j];
}

void cameraScreenBasis(float camX, float camY, float aspect, float out[4]) {
    const float fov = (aspect > 2.0f || aspect < 0.5f) ? 180.0f : 140.0f;
    M4 r = cameraMatrix(camX, camY);
    out[0] = -2.0f / fov * r.m[0];
    out[1] = -2.0f / fov * r.m[1];
    out[2] = 2.0f * aspect / fov * r.m[4];
    out[3] = 2.0f * aspect / fov * r.m[5];
}

float cameraFitScale(float camX, float camY, float aspect) {
    const float fov = (aspect > 2.0f || aspect < 0.5f) ? 180.0f : 140.0f;
    M4 r = cameraMatrix(camX, camY);
    const float p00 = -2.0f / fov;
    const float p11 = 2.0f * aspect / fov;
    const float a = p00 * r.m[0], b = p00 * r.m[1];
    const float c = p11 * r.m[4], d = p11 * r.m[5];
    const float det = a * d - b * c;
    float fit = 1.0f;
    if (std::fabs(det) > 1e-9f) {
        const float sx[4] = {1, -1, 1, -1}, sy[4] = {1, 1, -1, -1};
        for (int i = 0; i < 4; i++) {
            float wx = (d * sx[i] - b * sy[i]) / det;
            float wy = (-c * sx[i] + a * sy[i]) / det;
            fit = std::max(fit, std::max(std::fabs(wx), std::fabs(wy)) / (fov * 0.5f));
        }
    }
    return fit;
}

bool Renderer::ensureTarget() {
    if (fbo_ && fbW_ == w_ && fbH_ == h_) return true;
    if (!fbo_) {
        glGenFramebuffers(1, &fbo_);
        glGenTextures(1, &fbTex_);
    }
    glBindTexture(GL_TEXTURE_2D, fbTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w_, h_, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbTex_, 0);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) {
        fprintf(stderr, "renderer: no float colour target, grading stays 8-bit\n");
        fbW_ = fbH_ = 0;
        return false;
    }
    fbW_ = w_;
    fbH_ = h_;
    return true;
}

void Renderer::present() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w_, h_);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    // The pass writes already-encoded bytes; the hardware must not encode again
    // when the default framebuffer happens to be sRGB-capable.
    glDisable(GL_FRAMEBUFFER_SRGB);
    present_.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fbTex_);
    glUniform1i(present_.loc("uScene"), 0);
    glBindVertexArray(bgVao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    if (!encode_) glEnable(GL_FRAMEBUFFER_SRGB);
}

void Renderer::draw(const Bitmap& mask, const Shading& sh, float cameraX, float cameraY,
                    float scrollX, float scrollY, float animScale, float zoom,
                    const std::string& rampName, float speed) {
    uploadMask(mask);

    const float aspect = (float)w_ / (float)h_;
    const float fov = (aspect > 2.0f || aspect < 0.5f) ? 180.0f : 140.0f;
    M4 R = cameraMatrix(cameraX, cameraY);

    // sub_100008420 fits the rotated screen preimage inside the fov*fov grid
    // (v91..v94): the projection is scaled by the amount needed for the screen
    // corners to stay inside +-fov/2, with the fov/(fov-2) one-pixel margin.
    // Without this, a rolled camera leaves the extreme corners outside the grid.
    const float fit = cameraFitScale(cameraX, cameraY, aspect);
    // Recovered at 0x100008B8C: v94 = fit * fov/(fov-2) * (2*zoom + 1).
    const float z = std::max(0.05f, fit * (fov / (fov - 2.0f)) * (1.0f + 2.0f * zoom));

    // Recovered projection: visible width is `fov` bitmap pixels before the fit.
    M4 P{};
    P.m[0] = -2.0f * z / fov; // mirror is part of the recovered projection
    P.m[5] = 2.0f * aspect * z / fov;
    P.m[11] = -1.0f / 1000.0f;
    P.m[15] = 1.0f;
    M4 vp = mul(P, R);

    // The recovered instance grid is exactly fov*fov, centered on `scroll`
    // (sub_100008420: pixelsRect = scroll - fov/2, size fov, fov).
    const int gridI = (int)fov;

    // Scene goes to the linear RGBA16F target when available, so the shadow and
    // the background blend in linear light; present() then encodes and dithers.
    const bool hdr = havePresent_ && ensureTarget();
    glBindFramebuffer(GL_FRAMEBUFFER, hdr ? fbo_ : 0);
    const int enc = (hdr || !encode_) ? 0 : 1;

    glViewport(0, 0, w_, h_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    // ---- background: full-screen clip-space gradient (backgroundVertex/Fragment)
    glDisable(GL_BLEND);
    background_.use();
    glUniform4f(background_.loc("uTop"), sh.bgTop.r, sh.bgTop.g, sh.bgTop.b, 1);
    glUniform4f(background_.loc("uBottom"), sh.bgBottom.r, sh.bgBottom.g, sh.bgBottom.b, 1);
    glUniform1i(background_.loc("uEncode"), enc);
    glBindVertexArray(bgVao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // ---- per-pixel passes
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_);

    auto setCommon = [&](Program& p, float half, float vz) {
        p.use();
        uploadMat4(p.loc("uVP"), vp);
        glUniform4f(p.loc("uBitmapSize"), mask.w, mask.h, mask.w * 0.5f, mask.h * 0.5f);
        glUniform4i(p.loc("uBitmapSizeInt"), mask.w, mask.h, 0, 0);
        glUniform4i(p.loc("uPixelsRect"), (int)scrollX - gridI / 2, (int)scrollY - gridI / 2,
                    gridI, gridI);
        glUniform4f(p.loc("uTranslate"), scrollX - mask.w * 0.5f, scrollY - mask.h * 0.5f, 0.0f,
                    0.0f);
        float dx, dy;
        rampVector(rampName, dx, dy);
        glUniform4f(p.loc("uRampDirection"), dx * speed / fov, dy * speed / fov, 0.0f, 0.0f);
        glUniform4f(p.loc("uAnimation"), animScale, animScale * 1.25f - 0.75f,
                    animScale * 1.25f - 0.5f, 0.0f);
        glUniform4f(p.loc("uColor"), sh.ink.r, sh.ink.g, sh.ink.b, 1.0f);
        glUniform2f(p.loc("uHalf"), half, half);
        glUniform1f(p.loc("uVertexZ"), vz);
        glUniform1i(p.loc("uEncode"), enc);
        glUniform1i(p.loc("uMask"), 0);
        glBindVertexArray(vao_);
    };

    // ---- shadow: one 2.25-unit quad per ink pixel at z = -0.45, alpha blended
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    setCommon(shadow_, 1.125f, -0.45f);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, gridI * gridI);

    // ---- pixels: 0.9-unit quads, no blending
    glDisable(GL_BLEND);
    setCommon(pixel_, 0.45f, 0.0f);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, gridI * gridI);

    glBindVertexArray(0);
    if (hdr) present();
}

void Renderer::drawOverlay(const uint8_t* rgba, int w, int h, int scale) {
    if (!overlay_.id || !rgba || w <= 0 || h <= 0) return;
    if (scale < 1) scale = 1;
    if (!oTex_) {
        glGenTextures(1, &oTex_);
        glBindTexture(GL_TEXTURE_2D, oTex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, oTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (w != oTexW_ || h != oTexH_) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        oTexW_ = w;
        oTexH_ = h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }

    int pw = w * scale, ph = h * scale;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    overlay_.use();
    glUniform2f(overlay_.loc("uCenter"), 0.0f, 0.0f);
    glUniform2f(overlay_.loc("uHalf"), (float)pw / (float)w_, (float)ph / (float)h_);
    glUniform1i(overlay_.loc("uTex"), 0);
    glUniform1i(overlay_.loc("uEncode"), encode_ ? 1 : 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, oTex_);
    glBindVertexArray(bgVao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

void Renderer::readRgb(std::vector<uint8_t>& out) {
    out.resize((size_t)w_ * h_ * 3);
    std::vector<uint8_t> tmp((size_t)w_ * h_ * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, tmp.data());
    for (int y = 0; y < h_; y++)
        for (int x = 0; x < w_; x++) {
            const uint8_t* s = &tmp[((size_t)(h_ - 1 - y) * w_ + x) * 4];
            uint8_t* d = &out[((size_t)y * w_ + x) * 3];
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
        }
}
