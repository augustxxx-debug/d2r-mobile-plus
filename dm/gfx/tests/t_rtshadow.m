// 光追阴影核心的 Mac 单测（Metal 4，不启动游戏、不读游戏资源）：编译 nvn_rtshadow_msl.h 的全部核与合成管线；用一块地面 + 一块悬空方板的合成场景
// 走一遍生产路径——位置解码（SNORM16x4·exp2 与 FLOAT3）→ BLAS / TLAS → 太阳遮挡射线 → 蒙皮导出的裁剪坐标反投影 → 以绑定姿势为源 refit → 点光射线 → 最近命中 + 两趟模糊，
// 逐像素核对遮罩；纯函数（4×4 求逆、三角带展开、点光挑选）另测。不支持光追的机器返回 77（跳过）。
// Mac unit test of the ray-traced shadow core (Metal 4; the game is not launched, no game assets): compiles every kernel and the compositing pipeline of nvn_rtshadow_msl.h; a synthetic scene of a ground
// plane plus a floating square goes through the production path — position decode (SNORM16x4·exp2 and FLOAT3) → BLAS / TLAS → sun occlusion rays → unprojection of skinned clip positions →
// refit from the bind pose → point-light rays → nearest hit + two blur passes — and the masks are checked per pixel; the pure helpers (4×4 inverse, strip expansion, light picking) are tested separately.
// Returns 77 (skip) on machines without ray tracing.
#import <Metal/Metal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../nvn/nvn_rtshadow_util.h"
#include "../nvn/nvn_rtshadow_msl.h"
#define W 64
#define H 64
#define FAIL(...) do { printf("FAIL " __VA_ARGS__); printf("\n"); return 1; } while (0)
static id<MTLDevice> dev; static id<MTL4CommandQueue> queue; static id<MTL4CommandAllocator> alloc; static id<MTLResidencySet> resident;
// 一个命令缓冲、一个计算编码器、提交并等完 | one command buffer, one compute encoder, commit and wait
static int run(void (^body)(id<MTL4ComputeCommandEncoder> ce)) {
    id<MTL4CommandBuffer> cb = [dev newCommandBuffer]; [cb beginCommandBufferWithAllocator:alloc];
    id<MTL4ComputeCommandEncoder> ce = [cb computeCommandEncoder]; body(ce); [ce endEncoding]; [cb endCommandBuffer];
    dispatch_semaphore_t done = dispatch_semaphore_create(0); __block NSError *err = nil;
    MTL4CommitOptions *o = [MTL4CommitOptions new]; [o addFeedbackHandler:^(id<MTL4CommitFeedback> f) { err = f.error; dispatch_semaphore_signal(done); }];
    id<MTL4CommandBuffer> bufs[] = {cb}; [queue commit:bufs count:1 options:o];
    if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC))) { puts("GPU 没完成 | GPU did not complete"); return 1; }
    if (err) { printf("GPU 报错 | GPU error: %s\n", err.localizedDescription.UTF8String); return 1; }
    return 0;
}
static id<MTLBuffer> buf(const void *p, size_t n) { id<MTLBuffer> b = [dev newBufferWithLength:n options:MTLResourceStorageModeShared]; if (p) memcpy(b.contents, p, n); [resident addAllocation:b]; return b; }
static id<MTLTexture> tex(MTLPixelFormat f, int w, int h, MTLStorageMode sm) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:f width:w height:h mipmapped:NO]; d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite; d.storageMode = sm;
    id<MTLTexture> t = [dev newTextureWithDescriptor:d]; [resident addAllocation:t]; return t;
}
static MTL4PrimitiveAccelerationStructureDescriptor *blas_desc(id<MTLBuffer> pos, id<MTLBuffer> ib, uint32_t tris, int refit) {
    MTL4PrimitiveAccelerationStructureDescriptor *pd = [MTL4PrimitiveAccelerationStructureDescriptor new]; MTL4AccelerationStructureTriangleGeometryDescriptor *gd = [MTL4AccelerationStructureTriangleGeometryDescriptor new];
    gd.vertexBuffer = (MTL4BufferRange){pos.gpuAddress, pos.length}; gd.vertexFormat = MTLAttributeFormatFloat3; gd.vertexStride = 12; gd.triangleCount = tris; gd.opaque = YES;
    gd.indexBuffer = (MTL4BufferRange){ib.gpuAddress, ib.length}; gd.indexType = MTLIndexTypeUInt16;
    pd.geometryDescriptors = @[gd]; pd.usage = refit ? MTLAccelerationStructureUsageRefit : MTLAccelerationStructureUsageNone; return pd;
}
static float px(id<MTLTexture> t, int x, int y, int ch) { uint8_t v[4] = {0, 0, 0, 0}; [t getBytes:v bytesPerRow:t.width * (t.pixelFormat == MTLPixelFormatRGBA8Unorm ? 4 : t.pixelFormat == MTLPixelFormatRG8Unorm ? 2 : 1) fromRegion:MTLRegionMake2D(x, y, 1, 1) mipmapLevel:0]; return v[ch] / 255.f; }
// 像素 ↔ 世界：正交俯视，x = ndc.x·50、z = ndc.y·50、深度 = (50 − y)/100 | pixel ↔ world: orthographic top-down, x = ndc.x·50, z = ndc.y·50, depth = (50 − y)/100
static int gx_of(double x) { return (int)((x / 50 + 1) / 2 * W); }
static int gy_of(double z) { return (int)((1 - z / 50) / 2 * H); }
int main(void) { @autoreleasepool {
    // ---- 纯函数 | pure helpers ----
    { double m[16] = {2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 1, 2, 3, 1}, inv[16], id[16]; rts_inv4(m, inv);
      for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) { double s = 0; for (int k = 0; k < 4; k++) s += m[k * 4 + r] * inv[c * 4 + k]; id[c * 4 + r] = s; }
      for (int i = 0; i < 16; i++) if (fabs(id[i] - (i % 5 == 0 ? 1 : 0)) > 1e-9) FAIL("rts_inv4 [%d] = %g", i, id[i]); }
    {   // 光追内存（2026-10-28 真机内存 P1 之后）：各档字节估算、启动预算夹值、按预算的方案、系统内存告警状态机与迟滞 | ray-tracing memory (after the 2026-10-28 device memory P1): per-level byte estimates, start-up budget clamps, the budget plan, the memory-warning state machine and hysteresis
      rts_lvl_params p0 = rts_lvl_apply(0, 1, 2, 4, 0, 0, 1), p1 = rts_lvl_apply(1, 1, 2, 4, 0, 0, 1), p2 = rts_lvl_apply(2, 1, 2, 4, 0, 0, 1), p3 = rts_lvl_apply(3, 1, 2, 4, 0, 0, 1);
      rts_mem_est e = rts_mem_estimate(1996, 1404, p0, 64ull << 20, 10ull << 20);
      if (e.masks != 2ull * 1996 * 1404 || e.point != 499ull * 351 * 4 + 1996ull * 1404 * 4 || e.ao != 2ull * 998 * 702 + 1996ull * 1404 || e.mesh != 64ull << 20) FAIL("rts_mem_estimate 0 档 1996x1404：遮罩 %llu 点光 %llu AO %llu", (unsigned long long)e.masks, (unsigned long long)e.point, (unsigned long long)e.ao);
      e = rts_mem_estimate(1996, 1404, p2, 64ull << 20, 0); if (e.masks != 2ull * 1996 * 1404 + 998ull * 702 || e.ao) FAIL("rts_mem_estimate 2 档：半分辨率太阳遮罩另算、没有 AO");
      e = rts_mem_estimate(1996, 1404, p3, 64ull << 20, 0); if (e.point || e.ao) FAIL("rts_mem_estimate 3 档：没有点光遮罩、没有 AO");
      { uint32_t sz[3][2] = {{1595, 1122}, {1996, 1404}, {2852, 2006}}; rts_lvl_params pl[4] = {p0, p1, p2, p3};   // 交接表用：真机见过的渲染尺寸（档位 0 营地 / 原生档 + MetalFX / 原生不缩放）各档总量 | for the handover table: per-level totals at render sizes seen on the device
        for (int i = 0; i < 3; i++) { printf("光追内存估算 %ux%u：", sz[i][0], sz[i][1]); for (int l = 0; l < 4; l++) { rts_mem_est x = rts_mem_estimate(sz[i][0], sz[i][1], pl[l], 64ull << 20, 10ull << 20);
            printf("%s第 %d 档 %.1f MB（遮罩 %.1f、点光 %.1f、AO %.1f、网格 64、固定 10）", l ? "；" : "", l, rts_mem_est_total(&x) / 1048576.0, x.masks / 1048576.0, x.point / 1048576.0, x.ao / 1048576.0); } printf("\n"); } }
      if (rts_mem_budget(0) != UINT64_MAX || rts_mem_budget(100ull << 20) != RTS_MEM_BUDGET_MIN || rts_mem_budget(1000ull << 20) != 125ull << 20 || rts_mem_budget(4000ull << 20) != RTS_MEM_BUDGET_MAX) FAIL("rts_mem_budget 夹值");
      rts_mem_plan pn = rts_mem_plan_for(125ull << 20, 1996, 1404, p0, p3, 64ull << 20, 10ull << 20); if (pn.ao_off || pn.lvl_floor || pn.mesh_cap != 64ull << 20) FAIL("预算 125 MB、1996x1404：缺省放得下不该动");
      pn = rts_mem_plan_for(110ull << 20, 2852, 2006, p0, p3, 64ull << 20, 10ull << 20); if (!pn.ao_off || pn.lvl_floor || pn.mesh_cap != 64ull << 20 || pn.est > 110ull << 20) FAIL("预算 110 MB、原生：先只关 AO（%d %d %llu）", pn.ao_off, pn.lvl_floor, (unsigned long long)(pn.mesh_cap >> 20));
      pn = rts_mem_plan_for(80ull << 20, 2852, 2006, p0, p3, 64ull << 20, 10ull << 20); if (!pn.ao_off || pn.lvl_floor || pn.mesh_cap >= 64ull << 20 || pn.mesh_cap < RTS_MESH_CAP_MIN || pn.est > 80ull << 20) FAIL("预算 80 MB、原生：关 AO + 缩网格缓存");
      pn = rts_mem_plan_for(48ull << 20, 2852, 2006, p0, p3, 64ull << 20, 10ull << 20); if (!pn.ao_off || pn.lvl_floor != 3 || pn.mesh_cap != RTS_MESH_CAP_MIN) FAIL("预算 48 MB、原生：关 AO + 网格缓存 16 MB + 最低第 3 档");
      rts_memw m = {0}; if (rts_memw_warn(&m, 0, 3, 100) != 1 || m.ao_off || m.mesh_half) FAIL("第一次告警：最低第 1 档");
      if (rts_memw_tick(&m, 159) || !rts_memw_tick(&m, 160.5) || m.floor != 0 || m.ao_off) FAIL("单次告警 60 秒后放宽到 0");
      if (rts_memw_warn(&m, 2, 3, 200) != 3 || m.streak != 1) FAIL("现档 2 时告警：最低第 3 档（现档 + 1）");
      m = (rts_memw){0}; rts_memw_warn(&m, 0, 3, 100); if (rts_memw_warn(&m, 1, 3, 105) != 3 || !m.ao_off || !m.mesh_half || m.streak != 2) FAIL("10 秒内第二次：连续告警到最低档、关 AO、网格缓存减半");
      if (rts_memw_tick(&m, 164) || !rts_memw_tick(&m, 165.5) || m.floor != 2 || rts_memw_tick(&m, 190) || !rts_memw_tick(&m, 195.6) || m.floor != 1 || !m.ao_off || !rts_memw_tick(&m, 225.7) || m.floor != 0 || m.ao_off || m.mesh_half) FAIL("连续告警的迟滞：60 秒后一档、之后每 30 秒一档，到 0 才恢复 AO / 网格缓存");
      m = (rts_memw){0}; rts_memw_warn(&m, 0, 3, 100); if (rts_memw_warn(&m, 1, 3, 111) != 2 || m.ao_off) FAIL("隔 11 秒的第二次不算连续：只再降一档");
      rts_memw_warn(&m, 2, 3, 112); rts_memw_warn(&m, 3, 3, 113); if (m.floor != 3) FAIL("最低档不超过最高档");
    }
    { uint32_t o[24]; uint32_t t = rts_quad_expand(4, o);   // 七审 Q7-02：非索引四边形与原版光栅化同一拓扑 | seventh review Q7-02: non-indexed quads use the original rasterizer's topology
      if (t != 2 || o[0] != 0 || o[1] != 1 || o[2] != 2 || o[3] != 0 || o[4] != 2 || o[5] != 3) FAIL("rts_quad_expand 4: %u tris", t);
      t = rts_quad_expand(10, o); if (t != 4 || o[6] != 4 || o[7] != 5 || o[8] != 6 || o[9] != 4 || o[10] != 6 || o[11] != 7) FAIL("rts_quad_expand 10 (tail of 2 dropped): %u tris", t);
      if (rts_quad_expand(3, NULL) != 0 || rts_quad_expand(0, NULL) != 0 || rts_quad_expand(8, NULL) != 4) FAIL("rts_quad_expand counts"); }
    { uint16_t strip[] = {0, 1, 2, 2, 3, 4}; uint32_t out[12]; uint32_t t = rts_strip_expand(strip, 2, 6, out);   // 0,1,2 | 1,2,2 退化 | 2,2,3 退化 | 位置 3 是奇数 → 3,2,4（绕序交替）→ 2 个 | two triangles, two degenerate ones dropped; position 3 is odd → 3,2,4 (alternating winding)
      if (t != 2 || out[0] != 0 || out[1] != 1 || out[2] != 2 || out[3] != 3 || out[4] != 2 || out[5] != 4) FAIL("rts_strip_expand indexed: %u tris", t);
      uint16_t rsv[8] = {300, 301, 302, 303, 0xFFFF, 304, 305, 306}; uint32_t want[9] = {300, 301, 302, 302, 301, 303, 304, 305, 306}; t = rts_strip_expand(rsv, 2, 8, out);   // 重启后重新起算 | restart resets
      if (t != 3 || memcmp(out, want, sizeof want) || rts_strip_expand(rsv, 2, 8, NULL) != 3) FAIL("rts_strip_expand restart: %u tris", t);
      t = rts_strip_expand(NULL, 0, 5, out); if (t != 3 || out[6] != 2 || out[7] != 3 || out[8] != 4) FAIL("rts_strip_expand non-indexed: %u tris", t); }
    {   // 自动降档：超预算持续 1 秒降一档、到底停住；有余量持续 8 秒升一档；升上去很快又被迫降回来，升档等待翻倍 | automatic downgrade: 1 s over budget → one level down, stops at the bottom; 8 s of headroom → one level up; forced back down soon after a step up doubles the wait
        rts_gov g; rts_gov_init(&g, RTS_LVL_MAX); const char *why = NULL; double t = 0, B = 1000.0 / 60; int downs = 0, ups = 0, r;
        for (int i = 0; i < 60; i++, t += 1 / 60.0) if (rts_gov_step(&g, 3.0, 12.0, B, t, &why)) FAIL("gov: changed level within budget");
        for (int i = 0; i < 600; i++, t += 1 / 60.0) { r = rts_gov_step(&g, 4.0, 18.0, B, t, &why); downs += r > 0; if (r < 0) FAIL("gov: stepped up while over budget"); }
        if (g.lvl != RTS_LVL_MAX || downs != RTS_LVL_MAX) FAIL("gov: 10 s over budget should reach level %d (lvl %d, %d downs)", RTS_LVL_MAX, g.lvl, downs);
        double t0 = t; for (int i = 0; i < 60 * 30 && !ups; i++, t += 1 / 60.0) ups += rts_gov_step(&g, 1.0, 9.0, B, t, &why) < 0;
        if (!ups || g.lvl != RTS_LVL_MAX - 1 || t - t0 < 7.5 || t - t0 > 9.5) FAIL("gov: first step up after %.1f s (want ~8), lvl %d", t - t0, g.lvl);
        for (int i = 0; i < 180 && g.lvl != RTS_LVL_MAX; i++, t += 1 / 60.0) rts_gov_step(&g, 4.0, 18.0, B, t, &why);
        if (g.lvl != RTS_LVL_MAX || g.hold_up < 15.9) FAIL("gov: quick relapse should double the wait (lvl %d, hold %.0f)", g.lvl, g.hold_up);
        for (int i = 0; i < 60 * 5; i++, t += 1 / 60.0) rts_gov_step(&g, 3.0, 13.0, B, t, &why);   // 72–92%：两边都不动 | 72–92%: neither way
        if (g.lvl != RTS_LVL_MAX) FAIL("gov: moved inside the hysteresis band");
        rts_gov g2; rts_gov_init(&g2, RTS_LVL_MAX); t = 0;   // 整帧不知道（< 0）时只看光追单独占比 | frame time unknown (< 0): ray tracing share alone
        for (int i = 0; i < 300; i++, t += 1 / 60.0) rts_gov_step(&g2, 9.0, -1, B, t, &why); if (g2.lvl < 1) FAIL("gov: ray tracing alone over half the budget should step down");
        rts_gov g3; rts_gov_init(&g3, RTS_LVL_MAX); t = 0;   // 整帧超了但光追不重（< 12%）：不降（降了也没用） | frame over budget but ray tracing light (< 12%): no step down (it would not help)
        for (int i = 0; i < 300; i++, t += 1 / 60.0) rts_gov_step(&g3, 1.0, 25.0, B, t, &why); if (g3.lvl) FAIL("gov: stepped down although ray tracing is cheap");
        rts_gov g4; rts_gov_init(&g4, RTS_LVL_MAX); t = 0;   // 光追很贵（> 50%）但整帧有余量（78%）：不降（Mac 实测：AO 开时光追 9 ms、整帧 13 ms，原先误降） | ray tracing costly (> 50%) but the frame has headroom (78%): no step down (measured on the Mac: 9 ms ray tracing with AO, 13 ms frame, which used to step down)
        for (int i = 0; i < 300; i++, t += 1 / 60.0) rts_gov_step(&g4, 9.2, 13.1, B, t, &why); if (g4.lvl) FAIL("gov: stepped down although the frame has headroom");
        rts_lvl_params p0 = rts_lvl_apply(0, 1, 4, 2, 0, 1, 1), p1 = rts_lvl_apply(1, 1, 4, 2, 0, 1, 1), p2 = rts_lvl_apply(2, 1, 4, 2, 0, 1, 1), p3 = rts_lvl_apply(3, 1, 4, 2, 0, 1, 1);
        // 七审三轮：0 全开；1 太阳全分辨率 + 点光 ≤ 2 + 关 AO；2 太阳半分辨率 + 点光 1 盏排除随身光；3 再关点光 + 镂空照原版；哪一档都不是暂停 | seventh review round 3: 0 everything; 1 full-res sun + ≤ 2 point lights + AO off; 2 half-res sun + 1 point light excluding the character's own; 3 point lights off + alpha-tested original; no level is a pause
        if (p0.point_n != 4 || p0.point_div != 2 || p0.sun_div != 1 || !p0.soft || !p0.ao || p0.no_alpha || p0.excl_player
            || p1.point_n != 2 || p1.point_div != 4 || p1.sun_div != 1 || !p1.point_on || p1.ao || p1.no_alpha || p1.excl_player
            || p2.point_n != 1 || !p2.point_on || p2.sun_div != 2 || p2.soft || !p2.excl_player || p2.no_alpha
            || p3.point_on || p3.sun_div != 2 || !p3.no_alpha || p0.off || p1.off || p2.off || p3.off || RTS_LVL_MAX != 3) FAIL("rts_lvl_apply levels wrong");
    }
    { rts_player_track t = {.rec = -1}; float torch[3] = {40, 0, 0}, player[3] = {5, 0, 5}; int sure = 0;   // 相机沿 x 走：火把偏移在变、随身光偏移不变 | the camera walks along x: the torch's offset changes, the player light's does not
      for (int f = 0; f < 80; f++) { double p0[3] = {f * 0.5, 0, 0}; float pp[3] = {player[0] + f * 0.5f, player[1], player[2]}; sure = rts_player_feed(&t, 7, pp, p0); if (f < 59 && sure) FAIL("player light recognized too early (frame %d)", f); }
      if (!sure) FAIL("player light not recognized after 80 moving frames");
      for (int f = 0; f < 80; f++) { double p0[3] = {f * 0.5, 0, 0}; if (rts_player_feed(&t, 3, torch, p0)) FAIL("a static torch was taken for the player light (frame %d)", f); }
      for (int f = 0; f < 80; f++) { double p0[3] = {10, 0, 0}; if (rts_player_feed(&t, 9, player, p0)) FAIL("recognized while the camera stands still"); } }
    // 相机：列主序 VP（与 MSL float4x4 同） | camera: column-major VP (same as the MSL float4x4)
    double VP[16] = {0}; VP[0] = 1.0 / 50; VP[2 * 4 + 1] = 1.0 / 50; VP[1 * 4 + 2] = -0.01; VP[3 * 4 + 2] = 0.5; VP[15] = 1; double IA[16]; rts_inv4(VP, IA);
    { float L[16 * 4] = {0};   // 4 盏光：#1 在视线上且亮、#2 远、#3 亮度 0（忽略）、#0 偏离视线 | 4 lights: #1 on the view line and bright, #2 far, #3 zero brightness (ignored), #0 off the line
      float pos[4][3] = {{30, 0, 0}, {0, 20, 0}, {200, 0, 0}, {0, 0, 0}}, lum[4] = {1, 1, 1, 0};   // 得分 = 亮度 / (距视线² + 1e4)：#1 > #0 > #2 | score = brightness / (distance² + 1e4): #1 > #0 > #2
      for (int i = 0; i < 4; i++) { L[i * 16 + 0] = pos[i][0]; L[i * 16 + 1] = pos[i][1]; L[i * 16 + 2] = pos[i][2]; L[i * 16 + 4] = lum[i]; }
      int rec[4]; double best[4], p0[3]; int n = rts_pick_lights_cpu(L, 4, IA, 4, rec, best, p0);
      if (n != 3 || rec[0] != 1 || rec[1] != 0 || rec[2] != 2 || best[0] <= best[1] || best[1] <= best[2]) FAIL("rts_pick_lights_cpu: n=%d order %d %d %d", n, rec[0], rec[1], rec[2]);
      if (fabs(p0[0]) > 1e-6 || fabs(p0[2]) > 1e-6) FAIL("view line origin (%g, %g, %g)", p0[0], p0[1], p0[2]); }
    // ---- Metal ----
    dev = MTLCreateSystemDefaultDevice();
    if (!dev || ![dev supportsRaytracing] || ![dev supportsFamily:MTLGPUFamilyApple9]) { puts("SKIP t_rtshadow: no hardware ray tracing"); return 77; }
    NSError *e = nil;
    id<MTLLibrary> lib = [dev newLibraryWithSource:@(k_rts_msl) options:nil error:&e]; if (!lib) FAIL("MSL compile: %s", e.localizedDescription.UTF8String);
    id<MTLComputePipelineState> ps[10]; const char *names[10] = {"rts_decode", "rts_unproj", "rts_trace", "rts_trace_soft", "rts_blur", "rts_ptrace", "rts_pick", "rts_lcopy", "rts_lclear", "rts_lgather"};
    for (int i = 0; i < 10; i++) { ps[i] = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@(names[i])] error:&e]; if (!ps[i]) FAIL("pipeline %s: %s", names[i], e.localizedDescription.UTF8String); }
    id<MTLComputePipelineState> ps_decode = ps[0], ps_unproj = ps[1], ps_trace = ps[2], ps_soft = ps[3], ps_blur = ps[4], ps_ptrace = ps[5], ps_pick = ps[6], ps_lcopy = ps[7], ps_lclear = ps[8], ps_lgather = ps[9];   // 块里不能引用数组 | arrays cannot be referenced inside blocks
    { MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new]; pd.vertexFunction = [lib newFunctionWithName:@"rts_vs"]; pd.fragmentFunction = [lib newFunctionWithName:@"rts_fs"]; pd.colorAttachments[0].pixelFormat = MTLPixelFormatRG11B10Float;
      if (![dev newRenderPipelineStateWithDescriptor:pd error:&e]) FAIL("overlay pipeline: %s", e.localizedDescription.UTF8String); }
    queue = [dev newMTL4CommandQueueWithDescriptor:[MTL4CommandQueueDescriptor new] error:&e]; if (!queue) FAIL("MTL4 queue");
    resident = [dev newResidencySetWithDescriptor:[MTLResidencySetDescriptor new] error:&e]; [queue addResidencySet:resident];
    alloc = [dev newCommandAllocatorWithDescriptor:[MTL4CommandAllocatorDescriptor new] error:&e];
    MTL4ArgumentTableDescriptor *ad = [MTL4ArgumentTableDescriptor new]; ad.maxBufferBindCount = 5; ad.maxTextureBindCount = 3;
    id<MTL4ArgumentTable> at = [dev newArgumentTableWithDescriptor:ad error:&e];
    // 场景：地面 y=0 [−100,100]²（FLOAT3 原样）、方板 y=10 [−10,10]²（SNORM16x4·exp2(w·13)，w = 8/13 → 比例 256） | scene: ground y=0 (raw FLOAT3), square y=10 (SNORM16x4·exp2(w·13), w = 8/13 → scale 256)
    float ground[4][3] = {{-100, 0, -100}, {100, 0, -100}, {100, 0, 100}, {-100, 0, 100}}; uint16_t quad[6] = {0, 1, 2, 0, 2, 3};
    int16_t sq[4][4]; float sqw[4][3] = {{-10, 10, -10}, {10, 10, -10}, {10, 10, 10}, {-10, 10, 10}};
    for (int i = 0; i < 4; i++) { for (int c = 0; c < 3; c++) sq[i][c] = (int16_t)lrintf(sqw[i][c] / 256.f * 32767.f); sq[i][3] = (int16_t)lrintf(8.f / 13.f * 32767.f); }
    id<MTLBuffer> vb_g = buf(ground, sizeof ground), vb_s = buf(sq, sizeof sq), ib = buf(quad, sizeof quad);
    id<MTLBuffer> pos_g = buf(NULL, 4 * 12), pos_s = buf(NULL, 4 * 12), pos_slot = buf(NULL, 4 * 12), clip = buf(NULL, 4 * 16);
    uint32_t kg[4] = {4, 12, 0x22, 0}, ks[4] = {4, 8, 0x2b, 0}; id<MTLBuffer> kb_g = buf(kg, 16), kb_s = buf(ks, 16);
    MTLAccelerationStructureSizes szg = [dev accelerationStructureSizesWithDescriptor:blas_desc(pos_g, ib, 2, 0)], szs = [dev accelerationStructureSizesWithDescriptor:blas_desc(pos_s, ib, 2, 1)];
    id<MTLAccelerationStructure> as_g = [dev newAccelerationStructureWithSize:szg.accelerationStructureSize], as_s = [dev newAccelerationStructureWithSize:szs.accelerationStructureSize], as_slot = [dev newAccelerationStructureWithSize:szs.accelerationStructureSize];
    id<MTLBuffer> inst = buf(NULL, 2 * sizeof(MTLIndirectAccelerationStructureInstanceDescriptor));
    MTL4InstanceAccelerationStructureDescriptor *td = [MTL4InstanceAccelerationStructureDescriptor new]; td.instanceDescriptorType = MTLAccelerationStructureInstanceDescriptorTypeIndirect; td.instanceCount = 2;
    td.instanceDescriptorStride = sizeof(MTLIndirectAccelerationStructureInstanceDescriptor); td.instanceDescriptorBuffer = (MTL4BufferRange){inst.gpuAddress, inst.length};
    MTLAccelerationStructureSizes szt = [dev accelerationStructureSizesWithDescriptor:td]; id<MTLAccelerationStructure> tlas = [dev newAccelerationStructureWithSize:szt.accelerationStructureSize];
    NSUInteger scr = 4 << 20; id<MTLBuffer> scratch = [dev newBufferWithLength:scr options:MTLResourceStorageModePrivate];
    for (id r in @[as_g, as_s, as_slot, tlas, scratch]) [resident addAllocation:r];
    MTLIndirectAccelerationStructureInstanceDescriptor *di = inst.contents; memset(di, 0, 2 * sizeof *di);
    for (int i = 0; i < 2; i++) { di[i].transformationMatrix.columns[0].x = di[i].transformationMatrix.columns[1].y = di[i].transformationMatrix.columns[2].z = 1; di[i].options = MTLAccelerationStructureInstanceOptionOpaque; di[i].mask = 0xff; }
    di[0].accelerationStructureID = as_g.gpuResourceID; di[1].accelerationStructureID = as_s.gpuResourceID;
    // 深度：方板范围 0.4（y=10）、其余 0.5（y=0）；通过缓冲拷进 Depth32Float | depth: 0.4 (y=10) over the square, 0.5 (y=0) elsewhere; copied into a Depth32Float through a buffer
    float *dz = malloc(W * H * 4); for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { double wx = ((x + 0.5) / W * 2 - 1) * 50, wz = (1 - (y + 0.5) / H * 2) * 50; dz[y * W + x] = fabs(wx) < 10 && fabs(wz) < 10 ? 0.4f : 0.5f; }
    id<MTLBuffer> dzb = buf(dz, W * H * 4); free(dz);
    MTLTextureDescriptor *dd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:W height:H mipmapped:NO]; dd.usage = MTLTextureUsageShaderRead; dd.storageMode = MTLStorageModePrivate;
    id<MTLTexture> depth = [dev newTextureWithDescriptor:dd]; [resident addAllocation:depth];
    id<MTLTexture> mask = tex(MTLPixelFormatR8Unorm, W, H, MTLStorageModeShared), raw = tex(MTLPixelFormatRG8Unorm, W, H, MTLStorageModeShared), tmp = tex(MTLPixelFormatRG8Unorm, W, H, MTLStorageModeShared), pm = tex(MTLPixelFormatRGBA8Unorm, W / 2, H / 2, MTLStorageModeShared);
    // 常量：K（硬）/ KS（软）/ P（点光）/ B（模糊）/ U（反投影） | constants
    float K[64]; for (int i = 0; i < 16; i++) K[i] = (float)IA[i]; float sun[3] = {(float)M_SQRT1_2, (float)M_SQRT1_2, 0}; K[16] = sun[0]; K[17] = sun[1]; K[18] = sun[2]; K[19] = 0; ((uint32_t *)K)[20] = W; ((uint32_t *)K)[21] = H; K[22] = 0.6f; K[23] = 20.f;   // 软阴影的距离归一取 20：方板离地 14 → 半径 ≈ 0.7·12 = 8 像素（生产缺省 200 下这么近的遮挡体是硬的） | soft-shadow distance scale 20: the square 14 above the ground → radius ≈ 0.7·12 = 8 px (with the production default 200 such a near blocker stays hard)
    id<MTLBuffer> kb = buf(K, sizeof K);
    float KH[64]; memcpy(KH, K, sizeof KH); KH[23] = 0; id<MTLBuffer> kbh = buf(KH, sizeof KH);   // 硬阴影：第 23 位 = 半分辨率标志（0 = 全分辨率；软阴影的同一位是距离归一） | hard shadows: slot 23 = half-resolution flag (0 = full; the soft trace uses the same slot for its distance normalization)
    // 点光：记录按 texel 下标寻址、不按 4 对齐（游戏实测）。A 在 texel 4：(30,40,0) 颜色 2 半径 60；B 在 texel 9：(−200,5,0) 颜色 0.5 半径 10；texel 0 处颜色 0（不在分簇表里）。
    // 分簇表：簇 0 头 = 2 盏点光、起点 0，槽 0/1 = texel 4 / 9；簇 1 头 = 前三类结束于 3、1 盏点光，槽 256+3 = texel 4（重复，要去重）。期望列表 2 盏、A 排第一；玩家光按偏移认出 A，副本里 A 的颜色 texel 5 × 0.5
    // point lights: records are addressed by texel index, not 4-aligned (measured in the game). A at texel 4: (30,40,0) color 2 radius 60; B at texel 9: (−200,5,0) color 0.5 radius 10; color 0 at texel 0 (not in the cluster tables).
    // cluster tables: cluster 0 header = 2 point lights from slot 0, slots 0/1 = texel 4 / 9; cluster 1 header = first three kinds end at 3, 1 point light, slot 256+3 = texel 4 (a duplicate to dedupe). Expect 2 listed with A first; the player light matched to A by offset, A's color texel 5 halved in the copy
    float LR[16 * 4] = {0}; float *A = LR + 4 * 4, *Bl = LR + 9 * 4;
    A[0] = 30; A[1] = 40; A[2] = 0; A[4] = A[5] = A[6] = 2.0f; A[7] = 60;   // texel 4 位置、texel 5 颜色 + 半径 | texel 4 position, texel 5 color + radius
    Bl[0] = -200; Bl[1] = 5; Bl[2] = 0; Bl[4] = Bl[5] = Bl[6] = 0.5f; Bl[7] = 10;
    uint32_t HDR[2] = {2u << 24, 3u | (1u << 24)}; uint32_t *SL = calloc(512, 4); SL[0] = 4; SL[1] = 9; SL[256 + 3] = 4;
    id<MTLBuffer> lrec = buf(LR, sizeof LR), picks = buf(NULL, 128), prev = buf(NULL, 64), lcopyb = buf(NULL, sizeof LR), hdrb = buf(HDR, sizeof HDR), slotb = buf(SL, 512 * 4), seen = buf(NULL, 2048 * 4), list = buf(NULL, 257 * 4); free(SL);
    uint32_t LG[4] = {2, 512, 16, 256}; id<MTLBuffer> lgb = buf(LG, sizeof LG);
    float PK[40] = {0}; for (int i = 0; i < 16; i++) PK[i] = (float)IA[i]; PK[24] = PK[29] = PK[34] = PK[39] = 1;   /* 逆视矩阵 = 单位阵：记录已是世界坐标 | inverse view = identity: records already in world space */ uint32_t *pku = (uint32_t *)(PK + 16); pku[0] = 2; pku[1] = 1; ((float *)pku)[2] = 1.0f; pku[3] = 256;
    { double a0[4] = {0, 0, 0.5, 1}, o0[4]; for (int r = 0; r < 4; r++) { o0[r] = 0; for (int c = 0; c < 4; c++) o0[r] += IA[c * 4 + r] * a0[c]; } PK[20] = 30 - (float)(o0[0] / o0[3]); PK[21] = 40 - (float)(o0[1] / o0[3]); PK[22] = 0 - (float)(o0[2] / o0[3]); }
    id<MTLBuffer> pkb = buf(PK, sizeof PK);
    uint32_t LC[4] = {16, 0, 0, 0}; ((float *)LC)[1] = 0.5f; id<MTLBuffer> lcb = buf(LC, sizeof LC);
    float P[32] = {0}; for (int i = 0; i < 16; i++) P[i] = (float)IA[i]; ((uint32_t *)P)[16] = W / 2; ((uint32_t *)P)[17] = H / 2; ((uint32_t *)P)[18] = W; ((uint32_t *)P)[19] = H; P[20] = 0.6f;
    id<MTLBuffer> pb = buf(P, sizeof P);
    uint32_t B0[8] = {W, H, 1, 0, 0, 0, 0, 0}, B1[8] = {W, H, 0, 1, 0, 0, 0, 0}; ((float *)B0)[4] = ((float *)B1)[4] = 12.f; id<MTLBuffer> bb0 = buf(B0, 32), bb1 = buf(B1, 32);
    float U[20] = {0}; for (int i = 0; i < 16; i++) U[i] = (float)IA[i]; ((uint32_t *)U)[16] = 4; id<MTLBuffer> ub = buf(U, sizeof U);
    // 蒙皮导出的裁剪坐标：方板右移 30 后经 VP | skinned clip positions: the square shifted by +30 in x, through VP
    { float *c = clip.contents; for (int i = 0; i < 4; i++) { double w4[4] = {sqw[i][0] + 30, sqw[i][1], sqw[i][2], 1}; for (int r = 0; r < 4; r++) { double s = 0; for (int k = 0; k < 4; k++) s += VP[k * 4 + r] * w4[k]; c[i * 4 + r] = (float)s; } } }
    id<MTLBuffer> badb = buf(NULL, 16);   // 坏顶点计数（解码 / 反投影） | bad-vertex counters (decode / unproject)
    [resident commit];
    MTLSize tg = MTLSizeMake(W / 8, H / 8, 1), tt = MTLSizeMake(8, 8, 1);
    {   // 解码防坏数据：FLOAT4 的 w = 20 → exp2(260) = Inf、FLOAT3 里有 NaN → 收成原点并计数；正常点原样 | decode guard: FLOAT4 with w = 20 → exp2(260) = Inf, a NaN in FLOAT3 → collapsed to the origin and counted; sane points unchanged
        float bad4[8] = {1, 2, 3, 20, 1, 2, 3, 0}, bad3[6] = {NAN, 0, 0, 4, 5, 6}; uint32_t k4[4] = {2, 16, 0x2e, 0}, k3[4] = {2, 12, 0x22, 0};
        id<MTLBuffer> vb4 = buf(bad4, sizeof bad4), vb3 = buf(bad3, sizeof bad3), o4 = buf(NULL, 24), o3 = buf(NULL, 24), kb4 = buf(k4, 16), kb3 = buf(k3, 16), bz = buf(NULL, 16);
        if (run(^(id<MTL4ComputeCommandEncoder> ce) { [ce setArgumentTable:at]; [ce setComputePipelineState:ps_decode]; [at setAddress:bz.gpuAddress atIndex:3];
                [at setAddress:vb4.gpuAddress atIndex:0]; [at setAddress:o4.gpuAddress atIndex:1]; [at setAddress:kb4.gpuAddress atIndex:2]; [ce dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
                [at setAddress:vb3.gpuAddress atIndex:0]; [at setAddress:o3.gpuAddress atIndex:1]; [at setAddress:kb3.gpuAddress atIndex:2]; [ce dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)]; })) return 1;
        const float *r4 = o4.contents, *r3 = o3.contents; const uint32_t *bc = bz.contents;
        if (r4[0] != 0 || r4[1] != 0 || r4[2] != 0 || r4[3] != 1 || r4[5] != 3 || r3[0] != 0 || r3[3] != 4 || r3[5] != 6 || bc[0] != 2) FAIL("decode guard: (%g %g %g) (%g %g %g) / (%g %g %g) (%g %g %g), bad %u", r4[0], r4[1], r4[2], r4[3], r4[4], r4[5], r3[0], r3[1], r3[2], r3[3], r3[4], r3[5], bc[0]);
    }
    // 1. 深度拷贝、位置解码、BLAS、TLAS、太阳追踪（硬） | 1. depth copy, decode, BLAS, TLAS, sun trace (hard)
    if (run(^(id<MTL4ComputeCommandEncoder> ce) {
        [ce copyFromBuffer:dzb sourceOffset:0 sourceBytesPerRow:W * 4 sourceBytesPerImage:W * H * 4 sourceSize:MTLSizeMake(W, H, 1) toTexture:depth destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
        [ce setArgumentTable:at]; [ce setComputePipelineState:ps_decode]; [at setAddress:badb.gpuAddress atIndex:3];
        [at setAddress:vb_g.gpuAddress atIndex:0]; [at setAddress:pos_g.gpuAddress atIndex:1]; [at setAddress:kb_g.gpuAddress atIndex:2]; [ce dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
        [at setAddress:vb_s.gpuAddress atIndex:0]; [at setAddress:pos_s.gpuAddress atIndex:1]; [at setAddress:kb_s.gpuAddress atIndex:2]; [ce dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageAccelerationStructure visibilityOptions:MTL4VisibilityOptionDevice];
        [ce buildAccelerationStructure:as_g descriptor:blas_desc(pos_g, ib, 2, 0) scratchBuffer:(MTL4BufferRange){scratch.gpuAddress, scr}];
        [ce barrierAfterEncoderStages:MTLStageAccelerationStructure beforeEncoderStages:MTLStageAccelerationStructure visibilityOptions:MTL4VisibilityOptionDevice];
        [ce buildAccelerationStructure:as_s descriptor:blas_desc(pos_s, ib, 2, 1) scratchBuffer:(MTL4BufferRange){scratch.gpuAddress, scr}];
        [ce barrierAfterEncoderStages:MTLStageAccelerationStructure beforeEncoderStages:MTLStageAccelerationStructure visibilityOptions:MTL4VisibilityOptionDevice];
        [ce buildAccelerationStructure:tlas descriptor:td scratchBuffer:(MTL4BufferRange){scratch.gpuAddress, scr}];
        [ce barrierAfterEncoderStages:MTLStageAccelerationStructure beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        [ce setComputePipelineState:ps_trace]; [at setTexture:depth.gpuResourceID atIndex:0]; [at setTexture:mask.gpuResourceID atIndex:1]; [at setResource:tlas.gpuResourceID atBufferIndex:0]; [at setAddress:kbh.gpuAddress atIndex:1];
        [ce dispatchThreadgroups:tg threadsPerThreadgroup:tt];
    })) return 1;
    { const float *p = pos_s.contents; for (int i = 0; i < 4; i++) for (int c = 0; c < 3; c++) if (fabsf(p[i * 3 + c] - sqw[i][c]) > 0.05f) FAIL("decode SNORM16x4 v%d.%d = %g (want %g)", i, c, p[i * 3 + c], sqw[i][c]);
      p = pos_g.contents; if (fabsf(p[0] + 100) > 1e-6f || fabsf(p[11] - 100) > 1e-6f) FAIL("decode FLOAT3"); }
    // 太阳 (1,1,0)/√2：地面 x∈(−20,0) 被方板挡；方板顶面与远处地面亮 | sun (1,1,0)/√2: ground x∈(−20,0) is blocked by the square; the square's top and far ground are lit
    if (px(mask, gx_of(-10), gy_of(0), 0) > 0.01f) FAIL("sun: ground under the square should be shadowed");
    if (px(mask, gx_of(35), gy_of(0), 0) < 0.99f) FAIL("sun: far ground should be lit");
    if (px(mask, gx_of(0.8), gy_of(0.8), 0) < 0.99f) FAIL("sun: square top should be lit");
    if (px(mask, gx_of(-10), gy_of(30), 0) < 0.99f) FAIL("sun: ground beside the square (z=30) should be lit");
    {   // 太阳半分辨率追踪 + 深度感知上采样（生产缺省）：与全分辨率结果比，只允许影子边缘那一两个像素不同 | half-resolution sun trace + depth-aware upsampling (the production default): compared with the full-resolution result, only the one or two edge pixels may differ
        float KQ[64]; memcpy(KQ, K, sizeof KQ); KQ[23] = 1; ((uint32_t *)KQ)[20] = W / 2; ((uint32_t *)KQ)[21] = H / 2; id<MTLBuffer> kbq = buf(KQ, sizeof KQ);
        id<MTLTexture> mh = tex(MTLPixelFormatR8Unorm, W / 2, H / 2, MTLStorageModeShared), mf = tex(MTLPixelFormatR8Unorm, W, H, MTLStorageModeShared); uint32_t pu[4] = {W, H, W / 2, H / 2}; id<MTLBuffer> pub = buf(pu, 16);
        id<MTLComputePipelineState> ps_pup = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"rts_pup"] error:&e]; [resident commit];
        if (run(^(id<MTL4ComputeCommandEncoder> ce) { [ce setArgumentTable:at];
                [ce setComputePipelineState:ps_trace]; [at setTexture:depth.gpuResourceID atIndex:0]; [at setTexture:mh.gpuResourceID atIndex:1]; [at setResource:tlas.gpuResourceID atBufferIndex:0]; [at setAddress:kbq.gpuAddress atIndex:1];
                [ce dispatchThreadgroups:MTLSizeMake(W / 16, H / 16, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
                [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
                [ce setComputePipelineState:ps_pup]; [at setTexture:mh.gpuResourceID atIndex:0]; [at setTexture:mf.gpuResourceID atIndex:1]; [at setTexture:depth.gpuResourceID atIndex:2]; [at setAddress:pub.gpuAddress atIndex:0];
                [ce dispatchThreadgroups:MTLSizeMake(W / 8, H / 8, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)]; })) return 1;
        int diff = 0; for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) diff += fabsf(px(mf, x, y, 0) - px(mask, x, y, 0)) > 0.5f;
        if (px(mf, gx_of(-10), gy_of(0), 0) > 0.05f || px(mf, gx_of(35), gy_of(0), 0) < 0.95f) FAIL("half-res sun: shadow / lit spots wrong (%g, %g)", px(mf, gx_of(-10), gy_of(0), 0), px(mf, gx_of(35), gy_of(0), 0));
        if (diff > W * H / 50) FAIL("half-res sun: %d of %d pixels differ from the full-resolution mask", diff, W * H);
    }
    {   // 太阳 1/4 分辨率（自动降档第 3 档，k[23] = 4）：同样上采样回全分辨率，影子 / 亮处位置对，边缘差得多一些 | quarter-resolution sun (automatic level 3, k[23] = 4): upsampled back the same way, shadow / lit spots right, edges differ a bit more
        float KQ[64]; memcpy(KQ, K, sizeof KQ); KQ[23] = 4; ((uint32_t *)KQ)[20] = W / 4; ((uint32_t *)KQ)[21] = H / 4; id<MTLBuffer> kbq = buf(KQ, sizeof KQ);
        id<MTLTexture> mh = tex(MTLPixelFormatR8Unorm, W / 4, H / 4, MTLStorageModeShared), mf = tex(MTLPixelFormatR8Unorm, W, H, MTLStorageModeShared); uint32_t pu[4] = {W, H, W / 4, H / 4}; id<MTLBuffer> pub = buf(pu, 16);
        id<MTLComputePipelineState> ps_pup = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"rts_pup"] error:&e]; [resident commit];
        if (run(^(id<MTL4ComputeCommandEncoder> ce) { [ce setArgumentTable:at];
                [ce setComputePipelineState:ps_trace]; [at setTexture:depth.gpuResourceID atIndex:0]; [at setTexture:mh.gpuResourceID atIndex:1]; [at setResource:tlas.gpuResourceID atBufferIndex:0]; [at setAddress:kbq.gpuAddress atIndex:1];
                [ce dispatchThreadgroups:MTLSizeMake((W / 4 + 7) / 8, (H / 4 + 7) / 8, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
                [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
                [ce setComputePipelineState:ps_pup]; [at setTexture:mh.gpuResourceID atIndex:0]; [at setTexture:mf.gpuResourceID atIndex:1]; [at setTexture:depth.gpuResourceID atIndex:2]; [at setAddress:pub.gpuAddress atIndex:0];
                [ce dispatchThreadgroups:MTLSizeMake(W / 8, H / 8, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)]; })) return 1;
        int diff = 0; for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) diff += fabsf(px(mf, x, y, 0) - px(mask, x, y, 0)) > 0.5f;
        if (px(mf, gx_of(-10), gy_of(0), 0) > 0.05f || px(mf, gx_of(35), gy_of(0), 0) < 0.95f) FAIL("quarter-res sun: shadow / lit spots wrong (%g, %g)", px(mf, gx_of(-10), gy_of(0), 0), px(mf, gx_of(35), gy_of(0), 0));
        if (diff > W * H / 20) FAIL("quarter-res sun: %d of %d pixels differ from the full-resolution mask", diff, W * H);
    }
    {   // 光追 AO（实验性）：方板（y=10）边外紧挨着的地面被半球射线打到方板 → 变暗；远处地面、方板顶上 → 不暗；模糊 + 上采样后同样 | ray-traced AO (experimental): ground right outside the square (y=10) has hemisphere rays hitting the square → darker; far ground and the square's top → not; same after blur + upsampling
        float AK[32] = {0}; for (int i = 0; i < 16; i++) AK[i] = (float)IA[i]; ((uint32_t *)AK)[16] = W / 2; ((uint32_t *)AK)[17] = H / 2; AK[18] = 30.f; AK[19] = 0.3f; ((uint32_t *)AK)[20] = 0; ((uint32_t *)AK)[21] = 16; id<MTLBuffer> akb = buf(AK, sizeof AK);
        id<MTLTexture> ah = tex(MTLPixelFormatR8Unorm, W / 2, H / 2, MTLStorageModeShared), at2 = tex(MTLPixelFormatR8Unorm, W / 2, H / 2, MTLStorageModeShared), af = tex(MTLPixelFormatR8Unorm, W, H, MTLStorageModeShared);
        uint32_t pu[4] = {W, H, W / 2, H / 2}; id<MTLBuffer> pub = buf(pu, 16);
        id<MTLComputePipelineState> ps_ao = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"rts_ao"] error:&e], ps_aob = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"rts_aoblur"] error:&e],
            ps_pup = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"rts_pup"] error:&e];
        if (!ps_ao || !ps_aob || !ps_pup) FAIL("AO pipelines: %s", e.localizedDescription.UTF8String);
        [resident commit];
        if (run(^(id<MTL4ComputeCommandEncoder> ce) { [ce setArgumentTable:at];
                [ce setComputePipelineState:ps_ao]; [at setTexture:depth.gpuResourceID atIndex:0]; [at setTexture:ah.gpuResourceID atIndex:1]; [at setResource:tlas.gpuResourceID atBufferIndex:0]; [at setAddress:akb.gpuAddress atIndex:1];
                [ce dispatchThreadgroups:MTLSizeMake(W / 16, H / 16, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
                [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
                [ce setComputePipelineState:ps_aob]; [at setTexture:ah.gpuResourceID atIndex:0]; [at setTexture:at2.gpuResourceID atIndex:1]; [at setTexture:depth.gpuResourceID atIndex:2]; [at setAddress:pub.gpuAddress atIndex:0];
                [ce dispatchThreadgroups:MTLSizeMake(W / 16, H / 16, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
                [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
                [ce setComputePipelineState:ps_pup]; [at setTexture:at2.gpuResourceID atIndex:0]; [at setTexture:af.gpuResourceID atIndex:1]; [at setTexture:depth.gpuResourceID atIndex:2]; [at setAddress:pub.gpuAddress atIndex:0];
                [ce dispatchThreadgroups:MTLSizeMake(W / 8, H / 8, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)]; })) return 1;
        float near_ = px(ah, gx_of(13) / 2, gy_of(0) / 2, 0), far_ = px(ah, gx_of(-40) / 2, gy_of(-40) / 2, 0), top = px(ah, gx_of(0) / 2, gy_of(0) / 2, 0);
        if (!(near_ < 0.92f) || !(far_ > 0.98f) || !(top > 0.98f)) FAIL("AO: ground beside the square %g (want < 0.92), far ground %g, square top %g (want ~1)", near_, far_, top);
        float nf = px(af, gx_of(13), gy_of(0), 0), ff = px(af, gx_of(-40), gy_of(-40), 0), tf = px(af, gx_of(0), gy_of(0), 0);
        if (!(nf < 0.95f) || !(ff > 0.97f) || !(tf > 0.97f) || !(nf < ff)) FAIL("AO after blur + upsampling: beside %g, far %g, top %g", nf, ff, tf);
        printf("AO: beside the square %.2f (blurred %.2f), far ground %.2f, square top %.2f\n", near_, nf, far_, top);
    }
    if (px(mask, gx_of(-30), gy_of(0), 0) < 0.99f) FAIL("sun: ground x=−30 should be lit (ray passes the square's edge)");
    // 2. 反投影 + refit：导出的裁剪坐标 → 世界坐标 → 以绑定姿势 BLAS 为源 refit 到槽；TLAS 换成槽；阴影应移到 x∈(10,30) | 2. unproject + refit: shadow moves to x∈(10,30)
    di[1].accelerationStructureID = as_slot.gpuResourceID;
    if (run(^(id<MTL4ComputeCommandEncoder> ce) {
        [ce setArgumentTable:at]; [ce setComputePipelineState:ps_unproj]; [at setAddress:badb.gpuAddress atIndex:3];
        [at setAddress:clip.gpuAddress atIndex:0]; [at setAddress:pos_slot.gpuAddress atIndex:1]; [at setAddress:ub.gpuAddress atIndex:2]; [ce dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageAccelerationStructure visibilityOptions:MTL4VisibilityOptionDevice];
        [ce refitAccelerationStructure:as_s descriptor:blas_desc(pos_slot, ib, 2, 1) destination:as_slot scratchBuffer:(MTL4BufferRange){scratch.gpuAddress, scr} options:MTLAccelerationStructureRefitOptionVertexData];
        [ce barrierAfterEncoderStages:MTLStageAccelerationStructure beforeEncoderStages:MTLStageAccelerationStructure visibilityOptions:MTL4VisibilityOptionDevice];
        [ce buildAccelerationStructure:tlas descriptor:td scratchBuffer:(MTL4BufferRange){scratch.gpuAddress, scr}];
        [ce barrierAfterEncoderStages:MTLStageAccelerationStructure beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        [ce setComputePipelineState:ps_trace]; [at setTexture:depth.gpuResourceID atIndex:0]; [at setTexture:mask.gpuResourceID atIndex:1]; [at setResource:tlas.gpuResourceID atBufferIndex:0]; [at setAddress:kbh.gpuAddress atIndex:1];
        [ce dispatchThreadgroups:tg threadsPerThreadgroup:tt];
        // 点光 (30,40,0)：地面 x≈19.5 被移位后的方板挡，x=−35 不挡 | point light (30,40,0): ground x≈19.5 is blocked by the shifted square, x=−35 is not
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        [ce setComputePipelineState:ps_lclear]; [at setAddress:seen.gpuAddress atIndex:0]; [at setAddress:list.gpuAddress atIndex:1]; [ce dispatchThreadgroups:MTLSizeMake(32, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        [ce setComputePipelineState:ps_lgather]; [at setAddress:hdrb.gpuAddress atIndex:0]; [at setAddress:slotb.gpuAddress atIndex:1]; [at setAddress:seen.gpuAddress atIndex:2]; [at setAddress:list.gpuAddress atIndex:3]; [at setAddress:lgb.gpuAddress atIndex:4];
        [ce dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        [ce setComputePipelineState:ps_pick]; [at setAddress:lrec.gpuAddress atIndex:0]; [at setAddress:picks.gpuAddress atIndex:1]; [at setAddress:pkb.gpuAddress atIndex:2]; [at setAddress:prev.gpuAddress atIndex:3]; [at setAddress:list.gpuAddress atIndex:4];
        [ce dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        [ce setComputePipelineState:ps_lcopy]; [at setAddress:lrec.gpuAddress atIndex:0]; [at setAddress:lcopyb.gpuAddress atIndex:1]; [at setAddress:picks.gpuAddress atIndex:2]; [at setAddress:lcb.gpuAddress atIndex:3];
        [ce dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
        [ce setComputePipelineState:ps_ptrace]; [at setTexture:depth.gpuResourceID atIndex:0]; [at setTexture:pm.gpuResourceID atIndex:1]; [at setResource:tlas.gpuResourceID atBufferIndex:0]; [at setAddress:pb.gpuAddress atIndex:1]; [at setAddress:picks.gpuAddress atIndex:2];
        [ce dispatchThreadgroups:MTLSizeMake(W / 16, H / 16, 1) threadsPerThreadgroup:tt];
        // 软阴影：最近命中 + 两趟模糊 → raw / tmp / mask | soft shadows: nearest hit + two blur passes
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        [ce setComputePipelineState:ps_soft]; [at setTexture:raw.gpuResourceID atIndex:1]; [at setAddress:kb.gpuAddress atIndex:1]; [ce dispatchThreadgroups:tg threadsPerThreadgroup:tt];
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        [ce setComputePipelineState:ps_blur]; [at setTexture:raw.gpuResourceID atIndex:0]; [at setTexture:tmp.gpuResourceID atIndex:1]; [at setTexture:depth.gpuResourceID atIndex:2]; [at setAddress:bb0.gpuAddress atIndex:0]; [ce dispatchThreadgroups:tg threadsPerThreadgroup:tt];
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        [at setTexture:tmp.gpuResourceID atIndex:0]; [at setTexture:mask.gpuResourceID atIndex:1]; [at setAddress:bb1.gpuAddress atIndex:0]; [ce dispatchThreadgroups:tg threadsPerThreadgroup:tt];
    })) return 1;
    { const float *p = pos_slot.contents; for (int i = 0; i < 4; i++) { float want[3] = {sqw[i][0] + 30, sqw[i][1], sqw[i][2]}; for (int c = 0; c < 3; c++) if (fabsf(p[i * 3 + c] - want[c]) > 1e-2f) FAIL("unproject v%d.%d = %g (want %g)", i, c, p[i * 3 + c], want[c]); } }
    // 太阳遮罩此时是软阴影结果：移位后 x∈(10,30) 暗、x=−10 亮；硬边两侧应出现中间值 | the sun mask is now the soft result: x∈(10,30) dark, x=−10 lit; intermediate values must appear at the edges
    if (px(mask, gx_of(20), gy_of(0), 0) > 0.6f) FAIL("refit: ground x=20 should be (softly) shadowed by the shifted square (%g)", px(mask, gx_of(20), gy_of(0), 0));   // 硬判定在下面的 raw 上 | the hard check is on raw below
    if (px(mask, gx_of(-10), gy_of(0), 0) < 0.7f) FAIL("refit: ground x=−10 should be lit now (%g)", px(mask, gx_of(-10), gy_of(0), 0));
    if (px(raw, gx_of(20), gy_of(0), 0) > 0.01f || px(raw, gx_of(20), gy_of(0), 1) < 0.02f) FAIL("soft trace: hit + distance expected at x=20 (%g, %g)", px(raw, gx_of(20), gy_of(0), 0), px(raw, gx_of(20), gy_of(0), 1));
    { int mid = 0, hard_mid = 0; for (int x = 0; x < W; x++) { float v = px(mask, x, gy_of(0), 0), r = px(raw, x, gy_of(0), 0); if (v > 0.1f && v < 0.9f) mid++; if (r > 0.1f && r < 0.9f) hard_mid++; }
      if (hard_mid != 0) FAIL("hard mask has %d intermediate values", hard_mid); if (mid < 2) FAIL("soft mask has only %d intermediate values along the shadow row", mid); }
    { const int32_t *o = picks.contents; const float *of = picks.contents;
      if (o[20] != 2 || o[0] != 4 || o[1] != 9 || o[2] != -1 || fabsf(of[4] - 30) > 1e-3f || fabsf(of[5] - 40) > 1e-3f) FAIL("rts_lgather + rts_pick: n=%d rec %d %d %d first (%g, %g)", o[20], o[0], o[1], o[2], of[4], of[5]);
      if (o[21] != 4) FAIL("rts_pick: player light record %d (want texel 4)", o[21]);
      const float *lc = lcopyb.contents; if (fabsf(lc[5 * 4 + 0] - 1.0f) > 1e-5f || fabsf(lc[5 * 4 + 3] - 60.f) > 1e-5f || fabsf(lc[10 * 4 + 0] - 0.5f) > 1e-5f || fabsf(lc[4 * 4 + 0] - 30) > 1e-5f)
          FAIL("rts_lcopy: player color %g (want 1.0 = 2.0 x 0.5), radius %g, other %g", lc[20], lc[23], lc[40]);
      if (fabsf(((const float *)lrec.contents)[5 * 4] - 2.0f) > 1e-6f) FAIL("rts_lcopy wrote the source records"); }
    // 降档第 2 档起随身光不进选灯（PK.player_on 位 1，七审三轮第 4 条）：同一组灯只剩火把（texel 9），随身光照样认出（派生副本照样调弱） | from downgrade level 2 the character's own light is not picked (PK.player_on bit 1, seventh review round 3 item 4): of the same lights only the torch (texel 9) remains, and the player light is still recognized (still weakened in the derived copy)
    { ((uint32_t *)pkb.contents)[17] = 3; memset(prev.contents, 0, 64);
      if (run(^(id<MTL4ComputeCommandEncoder> ce) { [ce setArgumentTable:at]; [ce setComputePipelineState:ps_pick]; [at setAddress:lrec.gpuAddress atIndex:0]; [at setAddress:picks.gpuAddress atIndex:1]; [at setAddress:pkb.gpuAddress atIndex:2];
          [at setAddress:prev.gpuAddress atIndex:3]; [at setAddress:list.gpuAddress atIndex:4]; [ce dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)]; })) return 1;
      const int32_t *o = picks.contents; if (o[20] != 1 || o[0] != 9 || o[1] != -1 || o[21] != 4) FAIL("rts_pick excluding the player light: n=%d rec %d %d, player %d (want 1: 9, player 4)", o[20], o[0], o[1], o[21]);
      ((uint32_t *)pkb.contents)[17] = 1; }
    if (px(pm, gx_of(19.5) / 2, gy_of(0) / 2, 0) > 0.01f) FAIL("point light: ground x≈19.5 should be shadowed (%g)", px(pm, gx_of(19.5) / 2, gy_of(0) / 2, 0));
    if (px(pm, gx_of(-35) / 2, gy_of(0) / 2, 0) < 0.99f) FAIL("point light: ground x=−35 should be lit");
    if (px(pm, gx_of(-35) / 2, gy_of(0) / 2, 2) < 0.99f) FAIL("point light: unused channels stay lit");
    {   // 点光遮罩深度感知上采样：平坦地面上全分辨率与半分辨率一致（影子里暗、远处亮） | depth-aware point-mask upsampling: on flat ground the full-resolution result matches the half-resolution one (dark in the shadow, lit far away)
        id<MTLComputePipelineState> ps_pup = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"rts_pup"] error:&e]; if (!ps_pup) FAIL("rts_pup pipeline");
        id<MTLTexture> pmf = tex(MTLPixelFormatRGBA8Unorm, W, H, MTLStorageModeShared); uint32_t pu[4] = {W, H, W / 2, H / 2}; id<MTLBuffer> pub = buf(pu, 16); [resident commit];
        if (run(^(id<MTL4ComputeCommandEncoder> ce) { [ce setArgumentTable:at]; [ce setComputePipelineState:ps_pup]; [at setTexture:pm.gpuResourceID atIndex:0]; [at setTexture:pmf.gpuResourceID atIndex:1]; [at setTexture:depth.gpuResourceID atIndex:2];
                [at setAddress:pub.gpuAddress atIndex:0]; [ce dispatchThreadgroups:MTLSizeMake(W / 8, H / 8, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)]; })) return 1;
        if (getenv("T_PUP_DUMP")) { for (int x = 0; x < W; x++) printf("%d:%.2f/%.2f ", x, px(pm, x / 2, gy_of(0) / 2, 0), px(pmf, x, gy_of(0), 0)); printf("\n"); }
        if (px(pmf, gx_of(22), gy_of(0), 0) > 0.05f) FAIL("rts_pup: ground x=22 should stay shadowed (%g)", px(pmf, gx_of(22), gy_of(0), 0));
        { int mid = 0; for (int x = 0; x < W; x++) { float v = px(pmf, x, gy_of(0), 0); mid += v > 0.1f && v < 0.9f; } if (mid > 4) FAIL("rts_pup: %d soft pixels on a flat row (only the edge texels may blend)", mid); }
        if (px(pmf, gx_of(-35), gy_of(0), 0) < 0.95f) FAIL("rts_pup: ground x=−35 should stay lit (%g)", px(pmf, gx_of(-35), gy_of(0), 0));
    }
    // 镂空遮挡体（第四阶段）：判定库公共头 + 一个合成的「游戏片元着色器」（贴图 alpha < 阈值丢弃）+ 带 RTS_ALPHA 的追踪核一起编；方板 y=1、x∈[−1,1]、z∈[0,1]，
    // 插值 locn 2 = (u, v) = ((x+1)/2, z)，贴图 4×1 左半 alpha 0、右半 1；竖直向上的射线：左半穿过、右半挡住（距离 1）；判定序号 ~0 的记录按不透明处理（全挡）
    // Alpha-tested occluders (phase 4): the library's common head + a synthetic "game fragment shader" (discard when texture alpha < threshold) + the trace kernels with RTS_ALPHA compiled together; a square at y=1, x∈[−1,1], z∈[0,1],
    // interpolant locn 2 = (u, v) = ((x+1)/2, z), a 4×1 texture with alpha 0 on the left half and 1 on the right; vertical rays upward: the left half passes, the right half blocks (distance 1); a record with index ~0 counts as opaque (all block)
    {
        const char *asrc =
            "namespace rtsa_t { struct spvDescriptorSetBuffer0 { texture2d<float> tx [[id(0)]]; sampler sm [[id(1)]]; constant float4 *thr [[id(2)]]; }; struct main0_in { float4 m_2; };\n"
            "static inline bool main0(thread const main0_in& in, constant spvDescriptorSetBuffer0& d, float4 gl_FragCoord) { float a = d.tx.sample(d.sm, in.m_2.xy, level(0.0)).w; if (a < d.thr->x) { return false; } if (!(gl_FragCoord.w > 0.0)) { return false; } return true; } }\n"
            "static inline bool rts_alpha_eval(constant RtsAI &r, uint3 ix, float2 b, float4 fc) { switch (r.fs) { case 0: { rtsa_t::main0_in in; in.m_2 = rts_vl(r, ix, b, 2); return rtsa_t::main0(in, *(constant rtsa_t::spvDescriptorSetBuffer0 *)r.args, fc); } default: return true; } }\n";
        const char *tk = "kernel void t_alpha(instance_acceleration_structure as [[buffer(0)]], device float2 *o [[buffer(1)]], constant RtsAI *ai [[buffer(3)]], uint i [[thread_position_in_grid]]) {\n"
            "  if (i >= 64u) return; ray r; r.origin = float3(-1.0 + (float(i) + 0.5) / 32.0, 0.0, 0.5); r.direction = float3(0, 1, 0); r.min_distance = 0.0; r.max_distance = 10.0;\n"
            "  float d1, d2; bool h1 = rts_hit(r, as, ai, true, d1), h2 = rts_hit(r, as, ai, false, d2); o[i] = float2(h1 ? 1.0 : 0.0, h2 ? d2 : -1.0); }\n";
        NSString *src = [NSString stringWithFormat:@"#define RTS_ALPHA 1\n%s%s%s%s", k_rts_alpha_head, asrc, k_rts_msl, tk];
        id<MTLLibrary> alib = [dev newLibraryWithSource:src options:nil error:&e]; if (!alib) FAIL("RTS_ALPHA library compile: %s", e.localizedDescription.UTF8String);
        for (NSString *fn in @[@"rts_trace", @"rts_trace_soft", @"rts_ptrace", @"t_alpha"]) if (![dev newComputePipelineStateWithFunction:[alib newFunctionWithName:fn] error:&e]) FAIL("RTS_ALPHA pipeline %s: %s", fn.UTF8String, e.localizedDescription.UTF8String);
        id<MTLComputePipelineState> ps_ta = [dev newComputePipelineStateWithFunction:[alib newFunctionWithName:@"t_alpha"] error:&e];
        float qp[4][3] = {{-1, 1, 0}, {1, 1, 0}, {1, 1, 1}, {-1, 1, 1}}; float var[4][2][4];
        for (int v = 0; v < 4; v++) { var[v][0][0] = qp[v][0]; var[v][0][1] = qp[v][1]; var[v][0][2] = qp[v][2]; var[v][0][3] = 1; var[v][1][0] = (qp[v][0] + 1) / 2; var[v][1][1] = qp[v][2]; var[v][1][2] = var[v][1][3] = 0; }
        id<MTLBuffer> qpos = buf(qp, sizeof qp), qvar = buf(var, sizeof var), qout = buf(NULL, 64 * 8); float thr[4] = {0.5f, 0, 0, 0}; id<MTLBuffer> qthr = buf(thr, 16);
        MTLTextureDescriptor *td2 = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:4 height:1 mipmapped:NO]; td2.usage = MTLTextureUsageShaderRead; td2.storageMode = MTLStorageModeShared;
        id<MTLTexture> qtx = [dev newTextureWithDescriptor:td2]; uint8_t tp[16] = {255, 255, 255, 0, 255, 255, 255, 0, 255, 255, 255, 255, 255, 255, 255, 255}; [qtx replaceRegion:MTLRegionMake2D(0, 0, 4, 1) mipmapLevel:0 withBytes:tp bytesPerRow:16]; [resident addAllocation:qtx];
        MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new]; sd.supportArgumentBuffers = YES; sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterNearest; id<MTLSamplerState> qs = [dev newSamplerStateWithDescriptor:sd];
        uint64_t args[3] = {qtx.gpuResourceID._impl, qs.gpuResourceID._impl, qthr.gpuAddress}; id<MTLBuffer> qargs = buf(args, sizeof args);
        typedef struct { float lvp[16]; float vp[4]; float dr[2]; uint32_t fs, stride; uint64_t args, var, ib; uint32_t isz, locmask; uint8_t slot[16]; } t_ai;   // 与 nvn_rtshadow.inc rts_ai 同布局 | same layout as rts_ai in nvn_rtshadow.inc
        _Static_assert(sizeof(t_ai) == 144, "t_ai layout");
        t_ai rec = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, {0, 0, 64, 64}, {0, 1}, 0, 2, qargs.gpuAddress, qvar.gpuAddress, ib.gpuAddress, 2, 1u << 2, {0}}; rec.slot[2] = 1;
        id<MTLBuffer> qai = buf(&rec, sizeof rec);
        MTLAccelerationStructureSizes qsz = [dev accelerationStructureSizesWithDescriptor:blas_desc(qpos, ib, 2, 1)]; id<MTLAccelerationStructure> qas = [dev newAccelerationStructureWithSize:qsz.accelerationStructureSize];
        id<MTLBuffer> qinst = buf(NULL, sizeof(MTLIndirectAccelerationStructureInstanceDescriptor)); MTLIndirectAccelerationStructureInstanceDescriptor *qd = qinst.contents; memset(qd, 0, sizeof *qd);
        qd->options = MTLAccelerationStructureInstanceOptionNonOpaque; qd->mask = 0xff; qd->userID = 0; qd->accelerationStructureID = qas.gpuResourceID; qd->transformationMatrix.columns[0].x = qd->transformationMatrix.columns[1].y = qd->transformationMatrix.columns[2].z = 1;
        MTL4InstanceAccelerationStructureDescriptor *qtd = [MTL4InstanceAccelerationStructureDescriptor new]; qtd.instanceDescriptorType = MTLAccelerationStructureInstanceDescriptorTypeIndirect; qtd.instanceCount = 1;
        qtd.instanceDescriptorStride = sizeof(MTLIndirectAccelerationStructureInstanceDescriptor); qtd.instanceDescriptorBuffer = (MTL4BufferRange){qinst.gpuAddress, qinst.length};
        id<MTLAccelerationStructure> qtl = [dev newAccelerationStructureWithSize:[dev accelerationStructureSizesWithDescriptor:qtd].accelerationStructureSize];
        [resident addAllocation:qas]; [resident addAllocation:qtl]; [resident commit];
        for (int pass = 0; pass < 2; pass++) {
            if (pass == 1) ((t_ai *)qai.contents)->fs = ~0u;   // 抓不到参数块的记录：当不透明 | a record whose argument block was not captured: opaque
            if (run(^(id<MTL4ComputeCommandEncoder> ce) {
                [ce buildAccelerationStructure:qas descriptor:blas_desc(qpos, ib, 2, 1) scratchBuffer:(MTL4BufferRange){scratch.gpuAddress, scr}];
                [ce barrierAfterEncoderStages:MTLStageAccelerationStructure beforeEncoderStages:MTLStageAccelerationStructure visibilityOptions:MTL4VisibilityOptionDevice];
                [ce buildAccelerationStructure:qtl descriptor:qtd scratchBuffer:(MTL4BufferRange){scratch.gpuAddress, scr}];
                [ce barrierAfterEncoderStages:MTLStageAccelerationStructure beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
                [ce setArgumentTable:at]; [ce setComputePipelineState:ps_ta]; [at setResource:qtl.gpuResourceID atBufferIndex:0]; [at setAddress:qout.gpuAddress atIndex:1]; [at setAddress:qai.gpuAddress atIndex:3];
                [ce dispatchThreads:MTLSizeMake(64, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
            })) return 1;
            const float *o = qout.contents;
            for (int i = 0; i < 64; i++) { float x = -1.f + (i + 0.5f) / 32.f; int want = pass == 1 || x > 0;
                if ((o[i * 2] > 0.5f) != want) FAIL("alpha %s: ray x=%.3f any-hit %g (want %d)", pass ? "opaque fallback" : "judge", x, o[i * 2], want);
                if (want ? fabsf(o[i * 2 + 1] - 1.f) > 1e-3f : o[i * 2 + 1] >= 0) FAIL("alpha %s: ray x=%.3f closest %g", pass ? "opaque fallback" : "judge", x, o[i * 2 + 1]); }
        }
    }
    puts("PASS t_rtshadow: MSL compiles, decode / BLAS / TLAS / sun trace / unproject / refit / GPU light picking / player-light copy / point light / soft blur / alpha-tested judge (RTS_ALPHA) / automatic downgrade levels / quarter-resolution sun / ray-traced AO checked on a synthetic scene");
    return 0;
} }
