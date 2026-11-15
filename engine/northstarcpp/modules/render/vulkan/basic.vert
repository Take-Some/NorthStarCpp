#version 450
layout(location=0) in vec4 clip_position;
layout(location=1) in vec4 input_color;
layout(location=0) out vec4 vertex_color;
void main() { gl_Position=clip_position; vertex_color=input_color; }
