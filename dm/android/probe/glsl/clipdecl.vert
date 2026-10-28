// 与游戏顶点着色器同形：gl_PerVertex 里声明 ClipDistance / CullDistance 成员但不写（打包时去掉 ClipDistance / CullDistance capability，同 dmsh_vk 的裁剪结果）
// Same shape as the game's vertex shaders: gl_PerVertex declares ClipDistance / CullDistance members without writing them (the ClipDistance / CullDistance capabilities are removed when packing, as dmsh_vk's trimming leaves them)
#version 450
layout(location = 0) in vec4 a_pos;
out gl_PerVertex { vec4 gl_Position; float gl_PointSize; float gl_ClipDistance[1]; float gl_CullDistance[1]; };
void main() { gl_Position = a_pos; }
