#version 330 core
// RGBA overlay texture (panel = translucent black, glyphs = white), nearest sampled.
in vec2 vUV;
uniform sampler2D uTex;
uniform int uEncode;
out vec4 fragColor;

vec3 encodeSrgb(vec3 c) {
    bvec3 lo = lessThanEqual(c, vec3(0.0031308));
    vec3 hi = 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(hi, c * 12.92, vec3(lo));
}

void main() {
    vec4 c = texture(uTex, vUV);
    fragColor = vec4(uEncode == 1 ? encodeSrgb(c.rgb) : c.rgb, c.a);
}
