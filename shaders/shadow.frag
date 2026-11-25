#version 330 core
// Port of the recovered AIR shader `shadowFragment`:
//   a     = clamp(abs(modelPosition.xy) * 0.888889, 0, 1)      (8/9 = 1/1.125)
//   alpha = 0.375 * (1 - smoothstep(a.x)) * (1 - smoothstep(a.y))
//   color = uniforms.color with that alpha
in vec4 vModel;
uniform vec4 uColor;
uniform int uEncode;
out vec4 fragColor;

vec3 encodeSrgb(vec3 c) {
    bvec3 lo = lessThanEqual(c, vec3(0.0031308));
    vec3 hi = 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(hi, c * 12.92, vec3(lo));
}

void main() {
    vec2 a = clamp(abs(vModel.xy) * vec2(0.8888889), 0.0, 1.0);
    vec2 s = a * a * (3.0 - 2.0 * a);
    float alpha = 0.375 * (1.0 - s.x) * (1.0 - s.y);
    vec3 c = uColor.rgb;
    fragColor = vec4(uEncode == 1 ? encodeSrgb(c) : c, uColor.a * alpha);
}
