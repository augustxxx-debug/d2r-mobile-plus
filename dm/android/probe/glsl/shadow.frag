// 深度比较采样：sampler2DShadow 线性过滤，参考值 0.5；在像素中心与半像素偏移处各采一次 | depth compare sampling: sampler2DShadow with linear filtering, reference 0.5; sampled at the pixel centre and at a half-pixel offset
#version 450
layout(binding = 0) uniform sampler2DShadow sh;
layout(location = 0) out vec4 o;
void main() {
    vec2 uv = gl_FragCoord.xy / 64.0;
    o = vec4(texture(sh, vec3(uv, 0.5)), texture(sh, vec3(uv + vec2(0.5 / 64.0, 0.0), 0.5)), texture(sh, vec3(uv + vec2(0.25 / 64.0, 0.0), 0.5)), 1.0);
}
