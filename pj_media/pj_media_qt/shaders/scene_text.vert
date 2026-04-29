#version 440

// Text quad vertex (textured) — input in image-pixel space + UV + color tint.
// Maps pixel coords to NDC using frameSize, applies the shared viewTransform
// so labels track pan/zoom/letterbox alongside the image.

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;

layout(std140, binding = 0) uniform Uniforms {
    mat4 viewTransform;   // shared with image / line shaders
    vec4 frameSize;       // .xy = (width, height) of the underlying image, pixels
};

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

void main() {
    vec2 ndc;
    ndc.x = (a_pos.x / frameSize.x) * 2.0 - 1.0;
    ndc.y = -((a_pos.y / frameSize.y) * 2.0 - 1.0);

    gl_Position = viewTransform * vec4(ndc, 0.0, 1.0);
    v_uv = a_uv;
    v_color = a_color;
}
