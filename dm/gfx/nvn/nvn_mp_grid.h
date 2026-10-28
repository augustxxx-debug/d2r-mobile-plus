// 紫闪探针·网格的「连片」判据（真机长测 6a777d0：126 条【红】都是 64×36 网格里 1–2 个孤立点、位置每次不同，87 条在邪恶洞穴——迪亚波罗红色闪电与角色冰封球 / 连锁闪电叠加出 R≥200、B≥200、G≤60 的特效像素；
// 没写过的显存（真紫闪）是成块的 (255,0,255)）：品红点 ≥ MP_BLOCK_MIN 且占外接框（网格坐标）面积的 1/4 以上才记【红】，孤立 / 稀疏的点只计数
// "Contiguous" criterion of the magenta probe grid (device long test 6a777d0: all 126 red lines were 1–2 isolated points of the 64×36 grid at a different place each time, 87 of them in the Den of Evil - Diablo's red lightning over the character's Frozen Orb / Chain Lightning blends into effect pixels with R≥200, B≥200, G≤60;
// never-written memory (the real magenta flash) is a block of (255,0,255)): only >= MP_BLOCK_MIN magenta points filling at least 1/4 of their bounding box (grid coordinates) log a red line; isolated / sparse points are only counted
#pragma once
#include <stdint.h>
// 8 点（RC1 光追关 30 分钟真机局：97 条红线里有 3 条是施法光晕凑成的 4–5 点小团，2×2 / 2×5 格内，换十一个关卡都有；真缺贴图是成片的）
// 8 points (RC1 RT-off 30-minute device run: 3 of 97 red lines were 4–5-point clusters of spell glow within 2x2 / 2x5 cells, across eleven levels; a real missing texture is a sheet)
#define MP_BLOCK_MIN 8
static inline int mp_grid_block(uint32_t n, uint32_t x0, uint32_t x1, uint32_t y0, uint32_t y1) {
    if (n < MP_BLOCK_MIN || x1 < x0 || y1 < y0) return 0;
    uint64_t area = (uint64_t)(x1 - x0 + 1) * (uint64_t)(y1 - y0 + 1);
    return (uint64_t)n * 4 >= area;
}
