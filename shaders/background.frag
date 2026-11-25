#version 330 core
// Port of the recovered `backgroundFragment` (pass-through vertex colour).
in vec4 vColor;
uniform int uEncode;
out vec4 fragColor;

vec3 encodeSrgb(vec3 c) {
    bvec3 lo = lessThanEqual(c, vec3(0.0031308));
    vec3 hi = 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(hi, c * 12.92, vec3(lo));
}

void main() {
    vec3 c = vColor.rgb;
    fragColor = vec4(uEncode == 1 ? encodeSrgb(c) : c, 1.0);
}
