#version 330 core
// Port of the recovered `backgroundVertex`: the quad is already in clip space
// (the AIR function returns the raw vertex as [[position]] and never multiplies
// by the view-projection matrix). `y > 0` selects the top colour.
layout(location = 0) in vec2 aCorner; // [-1 .. 1]
uniform vec4 uTop;
uniform vec4 uBottom;
out vec4 vColor;
void main() {
    vColor = aCorner.y > 0.0 ? uTop : uBottom;
    gl_Position = vec4(aCorner, 0.0, 1.0);
}
