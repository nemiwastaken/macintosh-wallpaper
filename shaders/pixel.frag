#version 330 core
// Port of the recovered `pixelFragment`: returns uniforms.color unchanged.
uniform vec4 uColor;
uniform int uEncode; // 1 when the framebuffer is not sRGB-capable
out vec4 fragColor;

vec3 encodeSrgb(vec3 c) {
    bvec3 lo = lessThanEqual(c, vec3(0.0031308));
    vec3 hi = 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(hi, c * 12.92, vec3(lo));
}

void main() {
    vec3 c = uColor.rgb;
    fragColor = vec4(uEncode == 1 ? encodeSrgb(c) : c, uColor.a);
}
