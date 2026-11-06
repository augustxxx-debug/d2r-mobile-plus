// 全屏三角形（无输入）| full-screen triangle (no inputs)
#version 450
void main() { vec2 p = vec2((gl_VertexIndex & 1) * 4.0 - 1.0, (gl_VertexIndex >> 1) * 4.0 - 1.0); gl_Position = vec4(p, 0.0, 1.0); }
