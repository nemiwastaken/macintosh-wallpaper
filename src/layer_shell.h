#pragma once
#include <cstdint>
#include <list>
#include <vector>

// wlr-layer-shell backend: one background surface per Wayland output.
struct LayerOutput {
    void* surface = nullptr;   // wl_surface*
    void* layer = nullptr;     // zwlr_layer_surface_v1*
    void* eglWindow = nullptr; // wl_egl_window*
    void* eglSurface = nullptr;// EGLSurface
    void* output = nullptr;    // wl_output*
    uint32_t name = 0;         // wl_registry global name
    int width = 0, height = 0; // logical surface size from the configure
    int scale = 1;             // wl_output.scale
    bool configured = false;
    bool needResize = false;   // configure/scale changed since the EGL window was made
};

class LayerShell {
public:
    ~LayerShell();
    bool init(int layer = 1); // 0 background, 1 bottom, 2 top, 3 overlay
    // Process pending events; returns false if the connection died.
    bool dispatch();
    void resize(LayerOutput& o);      // (re)create the EGL surface after a configure
    bool makeCurrent(LayerOutput& o);
    void swap(LayerOutput& o);

    std::list<LayerOutput>& outputs() { return outputs_; }
    bool ok() const { return display_ != nullptr; }

private:
    void createSurface(LayerOutput& o);
    void destroySurface(LayerOutput& o);

    void* display_ = nullptr;   // wl_display*
    void* registry_ = nullptr;  // wl_registry*
    void* compositor_ = nullptr;// wl_compositor*
    void* layerShell_ = nullptr;// zwlr_layer_shell_v1*
    void* eglDisplay_ = nullptr;// EGLDisplay
    void* eglContext_ = nullptr;// EGLContext
    void* eglConfig_ = nullptr; // EGLConfig (as void*)
    std::list<LayerOutput> outputs_;
    int layer_ = 1;
    bool ready_ = false; // EGL is up, so new outputs can get surfaces immediately

    friend struct LayerShellCallbacks;
};
