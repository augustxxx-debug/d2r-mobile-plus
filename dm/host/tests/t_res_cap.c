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
      // 复审 F05：台阶只取严格低于基础的档 | re-audit F05: only caps strictly below the base
      CHECK(dm_res_auto_steps_for(4500000u) == 2); CHECK(dm_res_auto_steps_for(4000000u) == 2); CHECK(dm_res_auto_steps_for(3800000u) == 2);
      CHECK(dm_res_auto_steps_for(2800000u) == 1); CHECK(dm_res_auto_steps_for(2800001u) == 2); CHECK(dm_res_auto_steps_for(2073600u) == 0); CHECK(dm_res_auto_steps_for(0) == 0);
      CHECK(dm_res_auto_budget(2800000u, 1) == 2073600u);   // Duo 大屏：第一步就严格降到 207 万 | Duo large screen: the first step is strictly lower
      { const uint32_t bases[] = { 2073600u, 2800000u, 3800000u, 4000000u, 4500000u };
        for (int bi = 0; bi < 5; bi++) { uint32_t b = bases[bi]; int ns = dm_res_auto_steps_for(b); uint32_t prev = dm_res_auto_budget(b, 0); CHECK(prev == b);
          for (int k = 1; k <= ns; k++) { uint32_t cur = dm_res_auto_budget(b, k); CHECK(cur < prev); prev = cur; }   // 每一步严格变小 | every step strictly smaller
          CHECK(dm_res_auto_budget(b, ns + 1) == dm_res_auto_budget(b, ns)); } }
      { dm_res_auto_state rb = {0}; const uint64_t s2 = 1000000000ull; uint64_t tt = 100 * s2;
        for (int i = 0; i < 40; i++) { tt += 2 * s2; dm_res_auto_step(&rb, 1, 1, 0.5, 1, 0.5, 1, 0, tt); } CHECK(rb.step == 1);   // 只剩一档可降：停在第 1 步 | only one lower cap: stops at step 1
        for (int i = 0; i < 40; i++) { tt += 2 * s2; dm_res_auto_step(&rb, 0, 1, 0.5, 1, 0.5, 1, 0, tt); } CHECK(rb.step == 0); }   // 基础降到 207 万：步数被夹回 0 | base fell to 2.07 MP: the step is clamped to 0
      for (int i = 0; i < 10; i++) { t += 2 * sec; CHECK(dm_res_auto_step(&ra, 2, 0, 0.20, 1, 0.20, 1, 0, t) == 0); }      // 3D 没到下限：不降 | not at the floor: no step
      t += 2 * sec; CHECK(dm_res_auto_step(&ra, 2, 1, 0.06, 0, 0.0, 0, 0, t) == 0);                                           // 窗不够：不降 | not enough windows
      t += 2 * sec; CHECK(dm_res_auto_step(&ra, 2, 1, 0.06, 1, 0.0, 0, 0, t) == 1);                                           // 降一步 | one step down
      for (int i = 0; i < 4; i++) { t += 2 * sec; CHECK(dm_res_auto_step(&ra, 2, 1, 0.30, 1, 0.30, 1, 0, t) == 1); }         // 停判 10 秒内不再动 | held for 10 s
      t += 4 * sec; CHECK(dm_res_auto_step(&ra, 2, 1, 0.0, 1, 0.02, 1, 0, t) == 2);                                           // 60 秒规则降第二步 | second step via the 60 s rule
      for (int i = 0; i < 10; i++) { t += 2 * sec; dm_res_auto_step(&ra, 2, 1, 0.30, 1, 0.30, 1, 0, t); } CHECK(ra.step == 2);  // 最多 2 步 | capped at 2
      int n = 0; while (ra.step == 2 && n < 100) { t += 2 * sec; dm_res_auto_step(&ra, 2, 1, 0.0, 1, 0.0, 1, 1, t); n++; } CHECK(ra.step == 1 && n == 30);   // 30 个干净窗升一步 | 30 clean windows step up
      t += 12 * sec; CHECK(dm_res_auto_step(&ra, 2, 1, 0.06, 1, 0.0, 0, 0, t) == 2);                                         // 刚升 15 秒内又降 | dropped again within 15 s of the raise
      n = 0; t += 12 * sec; while (ra.step == 2 && n < 100) { t += 2 * sec; dm_res_auto_step(&ra, 2, 1, 0.0, 1, 0.0, 1, 1, t); n++; } CHECK(ra.step == 1 && n == 45);   // 下次升要 45 窗 | next raise needs 45
      n = 0; t += 12 * sec; while (ra.step == 1 && n < 100) { t += 2 * sec; dm_res_auto_step(&ra, 2, 0, 0.0, 1, 0.0, 1, 1, t); n++; } CHECK(ra.step == 0 && n == 30);   // 回到代际预算 | back to the generation budget
      t += 12 * sec; CHECK(dm_res_auto_step(&ra, 2, 1, 0.0, 1, 0.0, 1, 1, t) == 0); }   // 已在 0 步：干净窗不再升 | at step 0 clean windows do nothing
    // 光追开时启动即降一档（十三审闸 2）：档位映射、预留、局中封顶 | One tier down when RT is on at startup (13th review gate 2): level mapping, reserve, runtime cap
    { CHECK(dm_res_rt_budget(3, 0) == 2800000u); CHECK(dm_res_rt_budget(2, 0) == 2073600u); CHECK(dm_res_rt_budget(1, 0) == 2073600u);   // 原生 → 均衡、均衡 → 省电、省电不变 | Native → Balanced, Balanced → Eco, Eco unchanged
      CHECK(dm_res_rt_budget(0, 2800000u) == 2073600u);              // Duo 内屏自动（超大屏取均衡预算）→ 省电 | Duo inner screen Auto (huge screen takes Balanced) → Eco
      CHECK(dm_res_rt_budget(0, 2622u * 1206u) == 2800000u);         // 17 Pro 自动（基础 = 窗口 316 万）→ 均衡 | 17 Pro Auto (base = window 3.16 MP) → Balanced
      CHECK(dm_res_rt_budget(0, 2073600u) == 2073600u);              // 基础已 ≤ 207 万：不变 | base already ≤ 2.07 MP: unchanged
      // 窗口不变尺寸：预留 = 窗口按预算缩 | window never resizes: reserve = window shrunk to the budget
      uint64_t r = dm_res_rt_reserve(2622, 1206, 2622, 1206, 2800000u); CHECK(r == dm_res_cap_apply(2622, 1206, 2800000u)); CHECK((uint64_t)W(r) * H(r) <= 2800000u);
      CHECK(dm_res_rt_reserve(2622, 1206, 2622, 1206, 0) == ((uint64_t)1206 << 32 | 2622));   // 预算 0 = 不封顶 | budget 0 = no cap
      // Duo 内屏启动（折叠屏预留 3070x2160）原生 → 均衡：预留 2592x1468，比原预留少 43% 像素；内外屏在任一预算下的输出都装得下 | Duo inner-screen start (fold reserve 3070x2160) Native → Balanced: reserve 2592x1468, 43% fewer pixels; both screens fit at any budget
      r = dm_res_rt_reserve(3070, 2160, 2852, 2006, 2800000u); CHECK(W(r) == 2592 && H(r) == 1468);
      CHECK((uint64_t)W(r) * H(r) * 100 / (3070u * 2160u) < 58);
      const unsigned scr[][2] = { {2852, 2006}, {2034, 1398}, {2622, 1206}, {2868, 1320}, {2732, 2048}, {2400, 1080} };
      const uint32_t bud[] = { 2800000u, 2073600u };
      for (unsigned b = 0; b < 2; b++) { uint64_t res = dm_res_rt_reserve(3142, 2160, 2034, 1398, bud[b]);   // 外屏启动的折叠屏预留 | fold reserve from an outer-screen start
        CHECK(W(res) <= 3142 && H(res) <= 2160 && W(res) % 2 == 0 && H(res) % 2 == 0);
        for (unsigned i = 0; i < sizeof scr / sizeof *scr; i++) { double a = (double)scr[i][0] / scr[i][1]; if (a < 1.3 || a > 2.4) continue;
          uint64_t o = dm_res_rt_clamp((uint64_t)scr[i][1] << 32 | scr[i][0], bud[b], res);   // 局中档位改原生（输出 = 窗口） | mid-run switch to Native (output = window)
          CHECK(W(o) <= W(res) && H(o) <= H(res)); CHECK((uint64_t)W(o) * H(o) <= bud[b]); CHECK(o == dm_res_cap_apply(scr[i][0], scr[i][1], bud[b]));   // 宽高比 1.3–2.4 不必再按预留收 | aspects 1.3–2.4 need no extra fit
          CHECK(W(o) % 2 == 0 && H(o) % 2 == 0); } }
      // 局中封顶：输出已在预算内原样；超预留的逐边等比收进去 | runtime cap: an output already within budget is unchanged; one larger than the reserve is scaled into it
      CHECK(dm_res_rt_clamp((uint64_t)1206 << 32 | 1714, 2073600u, (uint64_t)1468 << 32 | 2592) == ((uint64_t)1206 << 32 | 1714));
      uint64_t o = dm_res_rt_clamp((uint64_t)2000 << 32 | 2000, 2800000u, (uint64_t)1468 << 32 | 2592); CHECK(H(o) <= 1468 && W(o) <= 2592 && W(o) % 2 == 0 && H(o) % 2 == 0);
      CHECK(dm_res_rt_clamp((uint64_t)2006 << 32 | 2852, 0, 0) == ((uint64_t)2006 << 32 | 2852)); }   // 预算 0：不动 | budget 0: untouched
    printf("t_res_cap: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
