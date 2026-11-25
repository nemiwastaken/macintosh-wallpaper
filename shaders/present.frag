#version 330 core
// Port of the compositor step that the macOS display path applies after the
// appex's Metal layer: encode linear light to sRGB and dither to 8 bits.
// Without the dither the paper gradient (two stops over the whole screen)
// quantises into visible stripes of 10-84 px depending on the theme channel.
uniform sampler2D uScene;
in vec2 vUV;
out vec4 fragColor;

vec3 encodeSrgb(vec3 c) {
    bvec3 lo = lessThanEqual(c, vec3(0.0031308));
    vec3 hi = 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(hi, c * 12.92, vec3(lo));
}

// Uniform noise in [0,1) from the pixel coordinate: integer hash, no trig.
float hash12(uvec2 p) {
    uint h = p.x * 0x8da6b343u ^ p.y * 0xd8163841u;
    h ^= h >> 15; h *= 0x2c1b3c6du;
    h ^= h >> 12; h *= 0x297a2d39u;
    h ^= h >> 15;
    return float(h & 0xffffffu) / 16777216.0;
}

void main() {
    vec3 v = encodeSrgb(max(texture(uScene, vUV).rgb, vec3(0.0)));
    // floor(v*255 + n): exact code values stay flat, fractional ones dither.
    fragColor = vec4(floor(v * 255.0 + hash12(uvec2(gl_FragCoord.xy))) / 255.0, 1.0);
}
