#version 450
layout(location=0) in vec4 vertex_color;
layout(location=0) out vec4 g_albedo;
layout(location=1) out vec4 g_normal;
layout(location=2) out vec4 g_surface;
#ifdef NS_GBUFFER_V2
layout(location=3) out vec4 g_auxiliary;
#endif
void main() {
    // Cubes and primitive meshes are unlit until authored PBR attributes
    // exist. Negative roughness encodes that material classification.
    g_albedo=vec4(clamp(vertex_color.rgb,0.0,1.0),1.0);
    g_normal=vec4(0.0,1.0,0.0,-0.7);
    g_surface=vec4(0.0);
#ifdef NS_GBUFFER_V2
    g_auxiliary=vec4(1.0,0.0,0.0,0.0);
#endif
}
