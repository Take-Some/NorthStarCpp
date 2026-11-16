#version 450
// Generic engine UI compositor: game-specific shader supplied via VFS.
layout(set=0,binding=0) uniform sampler2D ui_texture;
layout(push_constant) uniform UiConfig {
    vec4 rect;
    vec4 motion;
    vec4 config;
} fx;
layout(location=0) in vec2 out_uv;
layout(location=0) out vec4 out_color;
void main() {
    if(fx.config.z<=0.5) {
        out_color=texture(ui_texture,out_uv);
        return;
    }
    vec4 base=texture(ui_texture,vec2(out_uv.x,out_uv.y*0.5));
    vec4 overlay=texture(ui_texture,vec2(out_uv.x,0.5+out_uv.y*0.5));
    out_color=vec4(clamp(overlay.rgb+base.rgb*(1.0-overlay.a),0.0,1.0),
                   clamp(overlay.a+base.a*(1.0-overlay.a),0.0,1.0));
}
