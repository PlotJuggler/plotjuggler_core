#version 440

// The texture is an alpha-only (R8) glyph mask painted by QPainter. The
// per-vertex color provides the tint; mask.r modulates alpha so anti-aliasing
// from QPainter is preserved on the GPU side.

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D textTex;

void main() {
    float mask = texture(textTex, v_uv).r;
    fragColor = vec4(v_color.rgb, v_color.a * mask);
}
