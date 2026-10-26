// 整帧输出分辨率上限（dm_res_cap.h）：像素预算、宽高比、偶数、只缩不放、各档位；iPhone Duo 内屏与 iPhone 17 Pro Max 的实际尺寸。
// Whole-frame output resolution cap (dm_res_cap.h): pixel budget, aspect ratio, even sizes, shrink-only, every level; real iPhone Duo inner-screen and iPhone 17 Pro Max sizes.
#include "../dm_res_cap.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); } } while (0)
static unsigned W(uint64_t r) { return (unsigned)r; }
static unsigned H(uint64_t r) { return (unsigned)(r >> 32); }

static void check_cap(unsigned w, unsigned h, int level) {
    uint32_t budget = dm_res_cap_budget(level);
    uint64_t r = dm_res_cap_apply(w, h, budget);
    unsigned cw = W(r), ch = H(r);
    CHECK(cw >= 2 && ch >= 2);
    CHECK(cw % 2 == 0 && ch % 2 == 0);
    CHECK(cw <= w && ch <= h);                                   // 只缩不放 | shrink only
    if (!budget || (uint64_t)w * h <= budget) { CHECK(cw == w && ch == h); return; }   // 预算内原样 | within budget: unchanged
    CHECK((uint64_t)cw * ch <= budget);                           // 不超预算 | never over budget
    CHECK((uint64_t)(cw + 2) * ((uint64_t)(cw + 2) * h / w) > budget - budget / 50);   // 再大一档就超（或非常接近预算）：没有白白缩过头 | the next even width would exceed (or nearly reach) the budget: not shrunk needlessly
    double aspect = (double)w / h, got = (double)cw / ch;
    CHECK(fabs(got / aspect - 1) < 0.005);                        // 宽高比误差 < 0.5%（出图那边 < 1% 当相同铺满） | aspect error < 0.5% (the present side treats < 1% as equal and fills)
}

int main(void) {
    // 档位与预算 | levels and budgets
    CHECK(dm_res_cap_budget(0) == 4000000u); CHECK(dm_res_cap_budget(1) == 2073600u); CHECK(dm_res_cap_budget(2) == 2800000u); CHECK(dm_res_cap_budget(3) == 0);
    CHECK(dm_res_cap_budget(-1) == 4000000u); CHECK(dm_res_cap_budget(99) == 4000000u);   // 非法档位按自动 | invalid level behaves as Auto
    // 非法 / 空输入原样返回 | invalid or empty input is returned unchanged
    CHECK(dm_res_cap_apply(0, 1000, 100) == ((uint64_t)1000 << 32));
    CHECK(dm_res_cap_apply(1000, 0, 100) == 1000);
    CHECK(dm_res_cap_apply(2852, 2006, 0) == (((uint64_t)2006 << 32) | 2852));
    // iPhone Duo 内屏窗口 2852x2006（572 万像素）：自动档缩到 400 万以内，省电 207 万，均衡 280 万，原生不动
    // iPhone Duo inner-screen window 2852x2006 (5.72 MP): Auto shrinks within 4.0 MP, Eco 2.07 MP, Balanced 2.8 MP, Native unchanged
    for (int lv = 0; lv < DM_RES_LEVELS; lv++) check_cap(2852, 2006, lv);
    { uint64_t r = dm_res_cap_apply(2852, 2006, dm_res_cap_budget(0)); CHECK(W(r) >= 2380 && W(r) <= 2386); CHECK(H(r) >= 1672 && H(r) <= 1680); }
    { uint64_t r = dm_res_cap_apply(2852, 2006, dm_res_cap_budget(1)); CHECK(W(r) >= 1712 && W(r) <= 1720); }
    // Duo 启动预留 3142x2160 与外屏 2034x1398 | Duo startup reserve 3142x2160 and outer screen 2034x1398
    for (int lv = 0; lv < DM_RES_LEVELS; lv++) { check_cap(3142, 2160, lv); check_cap(2034, 1398, lv); }
    CHECK(dm_res_cap_apply(2034, 1398, dm_res_cap_budget(0)) == (((uint64_t)1398 << 32) | 2034));   // 外屏 284 万：自动档不动 | outer screen 2.84 MP: Auto leaves it alone
    // iPhone 17 Pro Max 2868x1320（379 万）：自动与原生不动，省电 / 均衡缩 | iPhone 17 Pro Max 2868x1320 (3.79 MP): Auto and Native unchanged, Eco / Balanced shrink
    CHECK(dm_res_cap_apply(2868, 1320, dm_res_cap_budget(0)) == (((uint64_t)1320 << 32) | 2868));
    for (int lv = 0; lv < DM_RES_LEVELS; lv++) check_cap(2868, 1320, lv);
    // 老机型 1952x900、iPad 2360x1640、掌机档 1568x720：全部档位自洽 | older phones 1952x900, iPad 2360x1640, handheld 1568x720: consistent at every level
    for (int lv = 0; lv < DM_RES_LEVELS; lv++) { check_cap(1952, 900, lv); check_cap(2360, 1640, lv); check_cap(1568, 720, lv); check_cap(1280, 720, lv); }
    // 极端：很小的预算与奇数尺寸 | extremes: tiny budget and odd sizes
    { uint64_t r = dm_res_cap_apply(2853, 2007, 8); CHECK(W(r) % 2 == 0 && H(r) % 2 == 0 && W(r) >= 2 && H(r) >= 2 && (uint64_t)W(r) * H(r) <= 8); }
    { uint64_t r = dm_res_cap_apply(7, 5, 10); CHECK(W(r) % 2 == 0 && H(r) % 2 == 0 && (uint64_t)W(r) * H(r) <= 10); }
    // 幂等：缩过一次的尺寸再按同预算缩，不再变 | idempotent: a size already capped stays put under the same budget
    for (int lv = 0; lv < DM_RES_LEVELS; lv++) { uint64_t r = dm_res_cap_apply(2852, 2006, dm_res_cap_budget(lv)); CHECK(dm_res_cap_apply(W(r), H(r), dm_res_cap_budget(lv)) == r); }
    // GPU 代际解析与自动档预算 | GPU generation parsing and the Auto budget
    { int g, p, m;
      CHECK(dm_res_gpu_class("Apple A20 Pro GPU", &g, &p, &m) && g == 20 && p == 1 && m == 0); CHECK(dm_res_cap_budget_auto(g, p, m) == 4000000u);
      // 超大屏：Duo 内屏 2852x2006 自动档取 280 万；17 Pro Max 2868x1320、Duo 外屏 2034x1398 不变；手动档不受屏幕影响 | Huge screen: Duo inner 2852x2006 Auto takes 2.8 MP; 17 Pro Max and the Duo outer screen unchanged; manual levels ignore the screen
      CHECK(dm_res_cap_budget_screen(0, 20, 1, 0, 2852, 2006) == 2800000u); CHECK(dm_res_cap_budget_screen(0, 20, 1, 0, 3142, 2160) == 2800000u);
      CHECK(dm_res_cap_budget_screen(0, 19, 1, 0, 2868, 1320) == 4000000u); CHECK(dm_res_cap_budget_screen(0, 20, 1, 0, 2034, 1398) == 4000000u);
      CHECK(dm_res_cap_budget_screen(3, 20, 1, 0, 2852, 2006) == 0); CHECK(dm_res_cap_budget_screen(1, 20, 1, 0, 2852, 2006) == 2073600u); CHECK(dm_res_cap_budget_screen(0, 16, 0, 0, 2852, 2006) == 2073600u);
      { uint64_t r = dm_res_cap_apply(2852, 2006, dm_res_cap_budget_screen(0, 20, 1, 0, 2852, 2006)); CHECK(W(r) >= 1990 && W(r) <= 1996); }
      CHECK(dm_res_gpu_class("Apple A19 Pro GPU", &g, &p, &m) && g == 19 && p == 1); CHECK(dm_res_cap_budget_auto(19, 1, 0) == 4000000u);
      CHECK(dm_res_gpu_class("Apple A19 GPU", &g, &p, &m) && g == 19 && p == 0); CHECK(dm_res_cap_budget_auto(19, 0, 0) == 3800000u);
      CHECK(dm_res_gpu_class("Apple A18 Pro GPU", &g, &p, &m) && g == 18 && p == 1); CHECK(dm_res_cap_budget_auto(18, 1, 0) == 3800000u);
      CHECK(dm_res_gpu_class("Apple A18 GPU", &g, &p, &m) && g == 18 && p == 0); CHECK(dm_res_cap_budget_auto(18, 0, 0) == 2800000u);
      CHECK(dm_res_gpu_class("Apple A17 Pro GPU", &g, &p, &m) && g == 17 && p == 1); CHECK(dm_res_cap_budget_auto(17, 1, 0) == 2800000u);
      CHECK(dm_res_gpu_class("Apple A16 GPU", &g, &p, &m) && g == 16); CHECK(dm_res_cap_budget_auto(16, 0, 0) == 2073600u); CHECK(dm_res_cap_budget_auto(15, 0, 0) == 2073600u);
      CHECK(dm_res_gpu_class("Apple M4 Pro GPU", &g, &p, &m) && m == 4 && p == 1 && g == 0); CHECK(dm_res_cap_budget_auto(0, 1, 4) == 4500000u);
      CHECK(dm_res_gpu_class("Apple M1", &g, &p, &m) && m == 1); CHECK(!dm_res_gpu_class("Intel Iris", &g, &p, &m)); CHECK(!dm_res_gpu_class(NULL, &g, &p, &m)); CHECK(dm_res_cap_budget_auto(0, 0, 0) == 4000000u);
      CHECK(dm_res_cap_budget_class(0, 16, 0, 0) == 2073600u); CHECK(dm_res_cap_budget_class(3, 16, 0, 0) == 0); CHECK(dm_res_cap_budget_class(2, 20, 1, 0) == 2800000u);
      // iPhone 17 Pro Max（A19 Pro，2868x1320）自动档不缩；iPhone 15 Pro（A17 Pro，2556x1179 ≈ 301 万）自动档缩到 280 万以内并保持比例 | 17 Pro Max unchanged on Auto; 15 Pro shrinks within 2.8 MP keeping aspect
      CHECK(dm_res_cap_apply(2868, 1320, dm_res_cap_budget_auto(19, 1, 0)) == (((uint64_t)1320 << 32) | 2868));
      { uint64_t r = dm_res_cap_apply(2556, 1179, dm_res_cap_budget_auto(17, 1, 0)); CHECK((uint64_t)W(r) * H(r) <= 2800000u); CHECK(fabs(((double)W(r) / H(r)) / (2556.0 / 1179) - 1) < 0.005); } }
    // 自动档闭环（纯函数）：不在下限不降；下限 + 10 秒掉帧 > 5% 降一步并停判 10 秒；最多 2 步；30 个干净窗升一步；刚升 15 秒内又降 → 下次要 45 窗；预算只降不升
    // Auto closed loop (pure): never steps down unless at the floor; floor + 10 s drops > 5% steps down once and holds 10 s; at most 2 steps; 30 clean windows step up; dropped again within 15 s of a raise → next raise needs 45
    { dm_res_auto_state ra = {0}; const uint64_t sec = 1000000000ull; uint64_t t = 100 * sec;
      CHECK(dm_res_auto_budget(4000000u, 0) == 4000000u); CHECK(dm_res_auto_budget(4000000u, 1) == 2800000u); CHECK(dm_res_auto_budget(4000000u, 2) == 2073600u);
      CHECK(dm_res_auto_budget(2073600u, 1) == 2073600u); CHECK(dm_res_auto_budget(2800000u, 2) == 2073600u);   // 不高于代际预算 | never above the generation budget
      for (int i = 0; i < 10; i++) { t += 2 * sec; CHECK(dm_res_auto_step(&ra, 0, 0.20, 1, 0.20, 1, 0, t) == 0); }      // 3D 没到下限：不降 | not at the floor: no step
      t += 2 * sec; CHECK(dm_res_auto_step(&ra, 1, 0.06, 0, 0.0, 0, 0, t) == 0);                                           // 窗不够：不降 | not enough windows
      t += 2 * sec; CHECK(dm_res_auto_step(&ra, 1, 0.06, 1, 0.0, 0, 0, t) == 1);                                           // 降一步 | one step down
      for (int i = 0; i < 4; i++) { t += 2 * sec; CHECK(dm_res_auto_step(&ra, 1, 0.30, 1, 0.30, 1, 0, t) == 1); }         // 停判 10 秒内不再动 | held for 10 s
      t += 4 * sec; CHECK(dm_res_auto_step(&ra, 1, 0.0, 1, 0.02, 1, 0, t) == 2);                                           // 60 秒规则降第二步 | second step via the 60 s rule
      for (int i = 0; i < 10; i++) { t += 2 * sec; dm_res_auto_step(&ra, 1, 0.30, 1, 0.30, 1, 0, t); } CHECK(ra.step == 2);  // 最多 2 步 | capped at 2
      int n = 0; while (ra.step == 2 && n < 100) { t += 2 * sec; dm_res_auto_step(&ra, 1, 0.0, 1, 0.0, 1, 1, t); n++; } CHECK(ra.step == 1 && n == 30);   // 30 个干净窗升一步 | 30 clean windows step up
      t += 12 * sec; CHECK(dm_res_auto_step(&ra, 1, 0.06, 1, 0.0, 0, 0, t) == 2);                                         // 刚升 15 秒内又降 | dropped again within 15 s of the raise
      n = 0; t += 12 * sec; while (ra.step == 2 && n < 100) { t += 2 * sec; dm_res_auto_step(&ra, 1, 0.0, 1, 0.0, 1, 1, t); n++; } CHECK(ra.step == 1 && n == 45);   // 下次升要 45 窗 | next raise needs 45
      n = 0; t += 12 * sec; while (ra.step == 1 && n < 100) { t += 2 * sec; dm_res_auto_step(&ra, 0, 0.0, 1, 0.0, 1, 1, t); n++; } CHECK(ra.step == 0 && n == 30);   // 回到代际预算 | back to the generation budget
      t += 12 * sec; CHECK(dm_res_auto_step(&ra, 1, 0.0, 1, 0.0, 1, 1, t) == 0); }   // 已在 0 步：干净窗不再升 | at step 0 clean windows do nothing
    printf("t_res_cap: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
