#version 330 core
// Port of the recovered Apple AIR shader `pixelVertex` (used by both the pixel and
// the shadow pipeline — the shipping appex builds the shadow pass with the same
// vertex function, only the fragment function and the vertex buffer differ).
layout(location = 0) in vec2 aCorner; // [-1 .. 1]; uHalf is the recovered +-0.45/+-1.125

uniform mat4 uVP;
uniform vec4 uBitmapSize;     // (w, h, w/2, h/2)
uniform ivec4 uBitmapSizeInt; // (w, h, 0, 0)
uniform ivec4 uPixelsRect;    // (scrollX - fov/2, scrollY - fov/2, fov, fov)
uniform vec4 uTranslate;      // scroll - (w/2, h/2)
uniform vec4 uRampDirection;  // normalised ramp direction * speed / fov
uniform vec4 uAnimation;      // (scale, scale*1.25-0.75, scale*1.25-0.5, 0)
uniform sampler2D uMask;

// Recovered vertex buffers: pixel = (+-0.45, +-0.45, 0), shadow = (+-1.125, +-1.125, -0.45).
uniform vec2 uHalf;
uniform float uVertexZ;

out vec4 vModel; // raw vertex, exactly what the AIR shader forwards as `modelPosition`

void main() {
    int x = uPixelsRect.x + gl_InstanceID % uPixelsRect.z;
    int y = uPixelsRect.y + gl_InstanceID / uPixelsRect.z;

    float coverage = 0.0;
    if (x >= 0 && x < uBitmapSizeInt.x && y >= 0 && y < uBitmapSizeInt.y) {
        if (texelFetch(uMask, ivec2(x, y), 0).r < 0.5) coverage = 1.0;
    }

    // Recovered: translate = scroll - bitmapSize.zw, p = bitmapSize.zw - texel - 0.5,
    // so the world position is (scroll.x - x - 1, scroll.y - y - 1). The -1 mirror is
    // carried by the projection matrix (negative X scale), exactly like the original.
    vec2 p = uBitmapSize.zw - vec2(float(x) + 0.5, float(y) + 0.5) - 0.5;
    vec2 q = uTranslate.xy + p;

    float ramp;
    if (uRampDirection.x != 0.0) {
        float v = q.x * uRampDirection.x;
        float t = clamp((v - uAnimation.y) / (uAnimation.z - uAnimation.y), 0.0, 1.0);
        ramp = 1.0 - (t * t * (3.0 - 2.0 * t));
    } else if (uRampDirection.y != 0.0) {
        float v = q.y * uRampDirection.y;
        float t = clamp((v - uAnimation.y) / (uAnimation.z - uAnimation.y), 0.0, 1.0);
        ramp = 1.0 - (t * t * (3.0 - 2.0 * t));
    } else {
        ramp = uAnimation.x;
    }

    vec3 local = vec3(aCorner * uHalf, uVertexZ);
    vec3 pos = vec3(q, 0.0) + (ramp * coverage) * local;
    vModel = vec4(local, 1.0);
    gl_Position = uVP * vec4(pos, 1.0);
}
