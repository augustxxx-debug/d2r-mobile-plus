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

// 持续性（十三审后 RC1 能耗对比局 bench 031334：1 条红 = 第 47 关、迪亚波罗红色闪电与角色连锁闪电叠加，64×36 里 8 点、外接框约 6×4 格，压线过了连片判据，
// 只在 26400 次取样中的 1 帧出现）：真缺贴图跟着物体在屏上一直在，法术光效逐帧换形。连片还要在连续 MP_PERSIST 次出图里出现、外接框相互重叠（差 1 格以内）才记红，
// 不够的只计「短暂连片」。返回这一次之后连续的次数（不是连片返回 0）
// Persistence (RC1 power-comparison run bench 031334 after the 13th review: 1 red line = level 47, Diablo's red lightning over the character's Chain Lightning, 8 of 64×36 points in a ~6×4-cell box,
// just over the contiguity line, in 1 frame of 26400 samples): a real missing texture stays on screen with its object, spell glow changes shape every frame. A contiguous block must also appear in MP_PERSIST
// consecutive presents with overlapping bounding boxes (within 1 cell) to log a red line; shorter runs only count as "brief blocks". Returns the run length after this sample (0 when not a block)
#define MP_PERSIST 3
typedef struct { uint64_t last; uint32_t run, x0, x1, y0, y1; } mp_persist_t;
static inline uint32_t mp_grid_persist(mp_persist_t *s, uint64_t ticket, int block, uint32_t x0, uint32_t x1, uint32_t y0, uint32_t y1) {
    if (!block) { s->run = 0; return 0; }
    int cont = s->run && s->last + 1 == ticket && x0 <= s->x1 + 1 && s->x0 <= x1 + 1 && y0 <= s->y1 + 1 && s->y0 <= y1 + 1;
    s->run = cont ? s->run + 1 : 1; s->last = ticket; s->x0 = x0; s->x1 = x1; s->y0 = y0; s->y1 = y1;
    return s->run;
}
