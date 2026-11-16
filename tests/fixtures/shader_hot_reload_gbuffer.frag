#version 450
layout(location=0) in vec2 vertex_uv;
layout(location=1) in vec3 vertex_normal;
layout(location=2) in vec4 vertex_tint;
layout(location=3) in vec4 vertex_params;
layout(location=4) in vec3 vertex_world;
layout(location=0) out vec4 output_color;
layout(location=1) out vec4 output_normal_roughness;
layout(location=2) out vec4 output_emissive_metallic;
void main() {
    output_color=vec4(0.0,1.0,0.0,1.0);
    output_normal_roughness=vec4(0.0,0.0,1.0,0.5);
    output_emissive_metallic=vec4(0.0,1.0,0.0,0.0);
}
