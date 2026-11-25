#version 330 core
// Final composite step. The scene is rendered into a linear RGBA16F target
// (the Metal layer's linear colour space); this pass is the one the system
// compositor would do: linear -> sRGB, then a 1-LSB dither before the 8-bit
// write, otherwise a smooth paper gradient bands into 10-80 px stripes.
layout(location = 0) in vec2 aCorner;
out vec2 vUV;
void main() {
    vUV = aCorner * 0.5 + 0.5;
    gl_Position = vec4(aCorner, 0.0, 1.0);
}
