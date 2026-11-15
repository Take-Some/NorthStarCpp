#version 450
layout(location=0) in vec4 clip_position;
layout(location=1) in vec2 input_uv;
layout(location=2) in vec3 input_normal;
layout(location=3) in vec4 input_tint;
layout(location=4) in vec4 input_params;
layout(location=5) in vec3 input_world;
layout(location=0) out vec2 vertex_uv;
layout(location=1) out vec3 vertex_normal;
layout(location=2) out vec4 vertex_tint;
layout(location=3) out vec4 vertex_params;
layout(location=4) out vec3 vertex_world;
void main() {
    gl_Position=clip_position;
    vertex_uv=input_uv;
    vertex_normal=input_normal;
    vertex_tint=input_tint;
    vertex_params=input_params;
    vertex_world=input_world;
}
