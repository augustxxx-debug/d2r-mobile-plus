// 片元四邻：shuffleXor(1) / shuffleXor(2) 取邻居的 FragCoord（Maxwell 四邻交换的形状），另记 dFdxFine 形式与子组大小 / 调用号
// Fragment quad: shuffleXor(1) / shuffleXor(2) of the neighbour's FragCoord (the shape of Maxwell's quad swaps), plus the dFdxFine form and subgroup size / invocation id
#version 450
#extension GL_KHR_shader_subgroup_basic : require
#extension GL_KHR_shader_subgroup_shuffle : require
layout(location = 0) out vec4 o0;
layout(location = 1) out vec4 o1;
void main() {
    float x = gl_FragCoord.x, y = gl_FragCoord.y;
    o0 = vec4(subgroupShuffleXor(x, 1u), subgroupShuffleXor(y, 2u), float(gl_SubgroupSize), float(gl_SubgroupInvocationID));
    o1 = vec4(dFdxFine(x), dFdyFine(y), dFdxFine(x * x), gl_HelperInvocation ? 1.0 : 0.0);
}
