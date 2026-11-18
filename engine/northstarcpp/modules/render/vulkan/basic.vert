#version 450
layout(location=0) in vec4 clip_position;
layout(location=1) in vec4 input_color;
layout(location=2) in vec3 input_world;
layout(location=0) out vec4 vertex_color;
layout(location=1) out vec3 vertex_world;
void main() { gl_Position=clip_position; vertex_color=input_color; vertex_world=input_world; }
