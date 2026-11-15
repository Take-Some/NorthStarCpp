#version 450
layout(location=0) in vec4 clip_position;
layout(location=1) in vec4 input_color;
layout(location=2) in vec3 input_world;
layout(location=3) in float input_surface;
layout(location=0) out vec4 vertex_color;
layout(location=1) out vec3 vertex_world;
layout(location=2) flat out int vertex_surface;
void main() {
    gl_Position=clip_position;
    vertex_color=input_color;
    vertex_world=input_world;
    vertex_surface=int(input_surface+0.5);
}
