#version 460
layout(location=0) out vec2 uv;
layout(location=1) out vec4 vview;
layout(push_constant) uniform MapView {
    // xy = uv-space center, zw = uv-space half extents
    vec4 view;
};
void main(){
    vec2 p = vec2((gl_VertexIndex<<1)&2, gl_VertexIndex&2);
    uv = view.xy + (p - 0.5) * 2.0 * view.zw;
    vview = view;
    gl_Position = vec4(p*2.0-1.0, 0.0, 1.0);
}
