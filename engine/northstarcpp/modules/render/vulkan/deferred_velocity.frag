#version 450
// Camera-geometry motion in pixel units: previous center minus current.
// Output RG16F. Moving object/skinned-motion override is a separate MRT
// stage, not inferred from depth (depth alone cannot describe deformation).
layout(set=0,binding=3) uniform sampler2D g_depth;
layout(push_constant) uniform MotionPush {
    vec4 current_camera;   // xyz + yaw
    vec4 current_project;  // pitch, near, far, fov scale
    vec4 previous_camera;  // xyz + yaw
    vec4 previous_project; // pitch, near, far, fov scale
    vec4 viewport;         // width, height, inverse width, inverse height
    vec4 options;          // history valid, subpixel delta X/Y, reserved
} camera;
layout(location=0) out vec2 pixel_velocity;

vec3 reconstruct_current(float depth,vec2 pixel) {
    float near_clip=camera.current_project.y;
    float far_clip=camera.current_project.z;
    float distance=far_clip*near_clip/
        max(1.e-5,far_clip-depth*(far_clip-near_clip));
    vec2 ndc=pixel*camera.viewport.zw*2.-1.;
    float scale=camera.current_project.w;
    float horizontal=ndc.x*distance*(camera.viewport.x/
                     camera.viewport.y)/scale;
    float vertical=-ndc.y*distance/scale;
    float pitch=camera.current_project.x;
    float forward=distance*cos(pitch)-vertical*sin(pitch);
    float y=vertical*cos(pitch)+distance*sin(pitch);
    float yaw=camera.current_camera.w;
    return camera.current_camera.xyz+vec3(
        horizontal*cos(yaw)+forward*sin(yaw),
        y,-horizontal*sin(yaw)+forward*cos(yaw));
}

void main() {
    pixel_velocity=vec2(0.);
    if(camera.options.x<.5)return;
    ivec2 p=ivec2(gl_FragCoord.xy);
    float depth=texelFetch(g_depth,p,0).r;
    if(depth>=.999999||depth<=0.)return; // sky/invalid geometry
    vec3 world=reconstruct_current(depth,gl_FragCoord.xy);
    vec3 delta=world-camera.previous_camera.xyz;
    float cy=cos(camera.previous_camera.w);
    float sy=sin(camera.previous_camera.w);
    float cp=cos(camera.previous_project.x);
    float sp=sin(camera.previous_project.x);
    float horizontal=delta.x*cy-delta.z*sy;
    float forward=delta.x*sy+delta.z*cy;
    float vertical=delta.y*cp-forward*sp;
    float distance=delta.y*sp+forward*cp;
    if(distance<=camera.previous_project.y ||
       distance>=camera.previous_project.z)return;
    vec2 ndc=vec2(
        horizontal*camera.previous_project.w/
            ((camera.viewport.x/camera.viewport.y)*distance),
        -vertical*camera.previous_project.w/distance);
    vec2 prev=(ndc*.5+.5)*camera.viewport.xy;
    vec2 motion=prev-gl_FragCoord.xy+camera.options.yz;
    if(any(isnan(motion))||any(isinf(motion)))return;
    pixel_velocity=clamp(motion,vec2(-32760.),vec2(32760.));
}
