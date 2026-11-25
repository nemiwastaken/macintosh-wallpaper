#include "layer_shell.h"
#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <poll.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

namespace {
int bufferScale(const LayerOutput& o) { return o.scale > 0 ? o.scale : 1; }
} // namespace

struct LayerShellCallbacks {
    static void onGlobal(void* data, wl_registry* reg, uint32_t name, const char* iface,
                         uint32_t version) {
        (void)version;
        LayerShell* s = (LayerShell*)data;
        if (!strcmp(iface, wl_compositor_interface.name)) {
            s->compositor_ = wl_registry_bind(reg, name, &wl_compositor_interface, 4);
        } else if (!strcmp(iface, zwlr_layer_shell_v1_interface.name)) {
            s->layerShell_ = wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface, 4);
        } else if (!strcmp(iface, wl_output_interface.name)) {
            // Version 2 gives scale + geometry + done; the surface size itself
            // comes from the layer-shell configure.
            wl_output* out = (wl_output*)wl_registry_bind(
                reg, name, &wl_output_interface, std::min(version, 2u));
            LayerOutput lo;
            lo.output = out;
            lo.name = name;
            s->outputs_.push_back(lo);
            LayerOutput* lp = &s->outputs_.back();
            static const wl_output_listener outListener = {
                LayerShellCallbacks::outGeometry, LayerShellCallbacks::outMode,
                LayerShellCallbacks::outDone, LayerShellCallbacks::outScale};
            wl_output_add_listener(out, &outListener, lp);
            if (s->ready_) {
                s->createSurface(*lp);
                wl_surface_commit((wl_surface*)lp->surface);
            }
        }
    }
    static void onGlobalRemove(void* data, wl_registry*, uint32_t name) {
        LayerShell* s = (LayerShell*)data;
        for (auto it = s->outputs_.begin(); it != s->outputs_.end(); ++it) {
            if (it->name != name) continue;
            s->destroySurface(*it);
            if (it->output) wl_output_destroy((wl_output*)it->output);
            s->outputs_.erase(it);
            break;
        }
    }

    static void outGeometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t,
                            const char*, const char*, int32_t) {}
    static void outMode(void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {}
    // A scale change does not alter the logical configure size, only the buffer.
    static void outScale(void* data, wl_output*, int32_t factor) {
        LayerOutput* o = (LayerOutput*)data;
        if (factor > 0 && factor != o->scale) {
            o->scale = factor;
            o->needResize = true;
            if (o->surface) wl_surface_set_buffer_scale((wl_surface*)o->surface, factor);
        }
    }
    static void outDone(void*, wl_output*) {}

    static void layerConfigure(void* data, zwlr_layer_surface_v1* layer, uint32_t serial,
                               uint32_t w, uint32_t h) {
        LayerOutput* o = (LayerOutput*)data;
        zwlr_layer_surface_v1_ack_configure(layer, serial);
        o->width = (int)w;
        o->height = (int)h;
        o->configured = w > 0 && h > 0;
        o->needResize = true;
    }
    static void layerClosed(void* data, zwlr_layer_surface_v1*) {
        LayerOutput* o = (LayerOutput*)data;
        o->configured = false;
    }
};

static const zwlr_layer_surface_v1_listener kLayerListener = {
    LayerShellCallbacks::layerConfigure, LayerShellCallbacks::layerClosed};

LayerShell::~LayerShell() {
    for (auto& o : outputs_) destroySurface(o);
    if (layerShell_) zwlr_layer_shell_v1_destroy((zwlr_layer_shell_v1*)layerShell_);
    if (compositor_) wl_compositor_destroy((wl_compositor*)compositor_);
    if (registry_) wl_registry_destroy((wl_registry*)registry_);
    if (eglDisplay_ != EGL_NO_DISPLAY) {
        eglMakeCurrent((EGLDisplay)eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (eglContext_) eglDestroyContext((EGLDisplay)eglDisplay_, (EGLContext)eglContext_);
        eglTerminate((EGLDisplay)eglDisplay_);
    }
    if (display_) wl_display_disconnect((wl_display*)display_);
}

void LayerShell::destroySurface(LayerOutput& o) {
    if (o.eglSurface) eglDestroySurface((EGLDisplay)eglDisplay_, (EGLSurface)o.eglSurface);
    if (o.eglWindow) wl_egl_window_destroy((wl_egl_window*)o.eglWindow);
    if (o.layer) zwlr_layer_surface_v1_destroy((zwlr_layer_surface_v1*)o.layer);
    if (o.surface) wl_surface_destroy((wl_surface*)o.surface);
    o.eglSurface = o.eglWindow = o.layer = o.surface = nullptr;
    o.configured = false;
    o.needResize = true;
}

void LayerShell::createSurface(LayerOutput& o) {
    int scale = bufferScale(o);
    o.surface = wl_compositor_create_surface((wl_compositor*)compositor_);
    wl_surface_set_buffer_scale((wl_surface*)o.surface, scale);
    o.layer = zwlr_layer_shell_v1_get_layer_surface(
        (zwlr_layer_shell_v1*)layerShell_, (wl_surface*)o.surface, (wl_output*)o.output,
        layer_ == 0 ? ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND
                    : (layer_ == 1 ? ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM
                                   : (layer_ == 2 ? ZWLR_LAYER_SHELL_V1_LAYER_TOP
                                                  : ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY)),
        "macintosh-wallpaper");
    zwlr_layer_surface_v1_add_listener((zwlr_layer_surface_v1*)o.layer, &kLayerListener, &o);
    zwlr_layer_surface_v1_set_anchor((zwlr_layer_surface_v1*)o.layer,
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                         ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                         ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                         ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone((zwlr_layer_surface_v1*)o.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        (zwlr_layer_surface_v1*)o.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
}

bool LayerShell::init(int layer) {
    layer_ = layer;
    display_ = wl_display_connect(nullptr);
    if (!display_) {
        fprintf(stderr, "layer-shell: cannot connect to Wayland\n");
        return false;
    }
    registry_ = wl_display_get_registry((wl_display*)display_);
    static const wl_registry_listener regListener = {LayerShellCallbacks::onGlobal,
                                                     LayerShellCallbacks::onGlobalRemove};
    wl_registry_add_listener((wl_registry*)registry_, &regListener, this);
    wl_display_roundtrip((wl_display*)display_);
    wl_display_roundtrip((wl_display*)display_);
    if (!compositor_ || !layerShell_) {
        fprintf(stderr, "layer-shell: compositor does not support wlr-layer-shell\n");
        return false;
    }

    eglDisplay_ = eglGetDisplay((EGLNativeDisplayType)display_);
    if (eglDisplay_ == EGL_NO_DISPLAY) {
        fprintf(stderr, "layer-shell: no EGL display\n");
        return false;
    }
    if (!eglInitialize((EGLDisplay)eglDisplay_, nullptr, nullptr)) {
        fprintf(stderr, "layer-shell: eglInitialize failed\n");
        return false;
    }
    static const EGLint cfgAttr[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE,
                                     EGL_OPENGL_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                     EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 0, EGL_NONE};
    EGLConfig cfg;
    EGLint n = 0;
    eglChooseConfig((EGLDisplay)eglDisplay_, cfgAttr, &cfg, 1, &n);
    if (n < 1) {
        fprintf(stderr, "layer-shell: no EGL config\n");
        return false;
    }
    eglConfig_ = cfg;
    eglBindAPI(EGL_OPENGL_API);
    static const EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
                                     EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                     EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    eglContext_ = eglCreateContext((EGLDisplay)eglDisplay_, cfg, EGL_NO_CONTEXT, ctxAttr);
    if (eglContext_ == EGL_NO_CONTEXT) {
        fprintf(stderr, "layer-shell: cannot create GL context\n");
        return false;
    }
    ready_ = true;

    for (auto& o : outputs_) {
        createSurface(o);
        wl_surface_commit((wl_surface*)o.surface);
    }
    // Wait for the first configure of every surface.
    for (int i = 0; i < 8; i++) {
        if (wl_display_roundtrip((wl_display*)display_) < 0) break;
        bool all = true;
        for (auto& o : outputs_) all = all && o.configured;
        if (all) break;
    }
    for (auto& o : outputs_)
        if (o.configured) resize(o);
    // Make the first configured surface current so the renderer can query the GL state.
    for (auto& o : outputs_)
        if (o.eglSurface) { makeCurrent(o); break; }
    return true;
}

bool LayerShell::dispatch() {
    if (!display_) return false;
    // Non-blocking event pump: read whatever the socket has (hotplug, configure),
    // without ever stalling the frame loop.
    if (wl_display_prepare_read((wl_display*)display_) == 0) {
        if (wl_display_flush((wl_display*)display_) < 0 && errno != EAGAIN) return false;
        struct pollfd pfd = {wl_display_get_fd((wl_display*)display_), POLLIN, 0};
        int pr = poll(&pfd, 1, 0);
        if (pr > 0 && (pfd.revents & POLLIN))
            wl_display_read_events((wl_display*)display_);
        else
            wl_display_cancel_read((wl_display*)display_);
    }
    if (wl_display_dispatch_pending((wl_display*)display_) < 0) return false;
    if (wl_display_flush((wl_display*)display_) < 0 && errno != EAGAIN) return false;
    return true;
}

void LayerShell::resize(LayerOutput& o) {
    int scale = bufferScale(o);
    int bw = o.width * scale, bh = o.height * scale;
    if (bw <= 0 || bh <= 0) return;
    if (!o.surface) return;
    if (getenv("MW_DEBUG"))
        fprintf(stderr, "  resize %dx%d scale=%d buffer=%dx%d\n", o.width, o.height, scale,
                bw, bh);
    wl_surface_set_buffer_scale((wl_surface*)o.surface, scale);
    if (!o.eglWindow) {
        o.eglWindow = wl_egl_window_create((wl_surface*)o.surface, bw, bh);
        o.eglSurface = eglCreateWindowSurface((EGLDisplay)eglDisplay_, (EGLConfig)eglConfig_,
                                              (EGLNativeWindowType)o.eglWindow, nullptr);
    } else {
        wl_egl_window_resize((wl_egl_window*)o.eglWindow, bw, bh, 0, 0);
    }
    o.needResize = false;
}

bool LayerShell::makeCurrent(LayerOutput& o) {
    return eglMakeCurrent((EGLDisplay)eglDisplay_, (EGLSurface)o.eglSurface,
                          (EGLSurface)o.eglSurface, (EGLContext)eglContext_);
}

void LayerShell::swap(LayerOutput& o) {
    eglSwapBuffers((EGLDisplay)eglDisplay_, (EGLSurface)o.eglSurface);
}
