#version 450
layout(location=0) out vec2 out_uv;
void main() {
    uint id=uint(gl_VertexIndex);
    vec2 uv=vec2(float((id<<1u)&2u),float(id&2u));
    // Vulkan viewport origin is upper-left for positive viewport height.
    gl_Position=vec4(uv*2.0-1.0,0.0,1.0);
    out_uv=uv;
}
