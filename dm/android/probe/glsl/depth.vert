// 深度梯度：全屏三角形，z 从左 0 到右 1 | depth gradient: full-screen triangle, z from 0 on the left to 1 on the right
#version 450
void main() { vec2 p = vec2((gl_VertexIndex & 1) * 4.0 - 1.0, (gl_VertexIndex >> 1) * 4.0 - 1.0); gl_Position = vec4(p, (p.x + 1.0) * 0.5, 1.0); }
