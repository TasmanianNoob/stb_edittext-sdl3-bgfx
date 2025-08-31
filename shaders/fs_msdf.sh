$input v_texcoord0, v_pxRange, v_color0

#include "bgfx_shader.sh"

SAMPLER2D(s_texColor, 0);

float screenPxRange(vec2 v_texcoord0) {
    float pxRange = 2.0;
    vec2 unitRange = vec2(pxRange, pxRange) / vec2(textureSize(s_texColor, 0));
    vec2 screenTexSize = vec2(1.0, 1.0) / fwidth(v_texcoord0);
    return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

void main() {
    vec3 msd = texture2D(s_texColor, v_texcoord0).rgb;
    float sd = median(msd.r, msd.g, msd.b);
    float screenPxDistance = screenPxRange(v_texcoord0) * (sd - 0.5);
    float opacity = clamp(screenPxDistance + 0.5, 0.0, 1.0);
    gl_FragColor = mix(vec4(0.0, 0.0, 0.0, 0.0), v_color0, opacity);
}