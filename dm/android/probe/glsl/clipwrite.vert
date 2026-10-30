// 真写 gl_ClipDistance（capability 保留）：设备 shaderClipDistance=0 时预期建管线失败或验证层报错 | really writes gl_ClipDistance (capability kept): expected to fail or be flagged when shaderClipDistance=0
#version 450
layout(location = 0) in vec4 a_pos;
out gl_PerVertex { vec4 gl_Position; float gl_PointSize; float gl_ClipDistance[1]; float gl_CullDistance[1]; };
void main() { gl_Position = a_pos; gl_ClipDistance[0] = a_pos.x; }
