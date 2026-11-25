#version 330 core
// Screen-space settings panel: the ±1 background quad is scaled to the panel size.
layout(location = 0) in vec2 aCorner;
uniform vec2 uCenter; // NDC
uniform vec2 uHalf;   // NDC half extents
out vec2 vUV;
void main() {
    gl_Position = vec4(aCorner * uHalf + uCenter, 0.0, 1.0);
    vUV = vec2(aCorner.x * 0.5 + 0.5, 0.5 - aCorner.y * 0.5);
}
