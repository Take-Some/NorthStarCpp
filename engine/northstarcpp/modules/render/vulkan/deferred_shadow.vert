#version 450
// World-space depth-only shader for native and GPU-skinned mesh buffers.
layout(location=0) in vec3 world_position;
layout(set=0,binding=0,std140) uniform ShadowMatrices {
    mat4 light_vp[3];
    vec4 cascade_splits;
} matrices;
layout(push_constant) uniform ShadowCascade { uint cascade_index; } push_data;
void main() { gl_Position=matrices.light_vp[push_data.cascade_index]*vec4(world_position,1.0); }
