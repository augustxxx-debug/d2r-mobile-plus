// 光追阴影：光追画面与原版画面不得逐帧来回切（真机 d42c8ee 营地：静态物件的影子忽有忽无）。
// 光追帧把阴影图清回全亮、整屏乘遮罩；本帧有阴影绘制没进 TLAS（新网格待建、配额满…）整帧照原版。两者的影子本来就不完全一样
// （光追给营地立柱、横梁投出原版没有的影子），逐帧交替时这些影子就一闪一闪。真机 5055–5314 帧：259 帧里约 100 帧原版、159 帧光追混排。
// 修法：从光追切回原版之后，要连续若干帧完整（且能清回全亮）才再切回光追；每中断一次要求翻倍、上限 1920 帧，连续光追 1200 帧再减半。
// 生产 rts_draw / rts_set_rt / rts_build_and_trace / rts_frame_begin + 真 Metal 设备；每帧真提交、回读阴影图：1.0 = 本帧光追画面，0.25 = 原版。
// Ray-traced shadows: the picture must not flip between ray-traced and original every frame (device d42c8ee in camp: static objects' shadows blinking on and off).
// A ray-traced frame clears the shadow map back to fully lit and multiplies the mask over the screen; a frame with a shadow draw outside the TLAS (new mesh pending, quota full…) stays original as a whole. Their
// shadows are not identical (ray tracing casts shadows from the camp's posts and beams that the original does not), so alternating frames make those shadows blink. Device frames 5055-5314: ~100 original and 159 traced frames interleaved within 259.
// Fix: after switching back to original, a number of consecutive complete (restorable) frames is required before switching to ray tracing again; it doubles with every break up to 1920 frames, and halves after 1200 frames of uninterrupted ray tracing.
// Production rts_draw / rts_set_rt / rts_build_and_trace / rts_frame_begin + a real Metal device; every frame is really submitted and the shadow map read back: 1.0 = ray-traced picture this frame, 0.25 = original.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
#if DM_NVN_M3
int main(void) { printf("SKIP：旧接口版没有光追阴影模块\n"); return 77; }
#else
#ifndef RTS_SWITCHES
#define RTS_SWITCHES() (-1LL)   // d42c8ee 及更早：不记画面切换次数 | d42c8ee and earlier: picture switches are not counted
#define RTS_NEED() (-1)
#define RTS_HYST_SET(v) ((void)(v))
#define RTS_HYST_RESET() ((void)0)
#endif
static int fail, count;
#define EXPECT(name, want, got) do { long long w_=(long long)(want), g_=(long long)(got); count++; printf("%s 期望 %lld → 真结果 %lld %s\n", name, w_, g_, w_==g_?"✓":"✗"); fail += w_ != g_; } while (0)
static uint8_t *g_pool, *g_mat, *g_cb3, *g_cam, *g_vtx, *g_idx;
static NvProg *g_pr; static tex_t g_sdepth, g_mdepth, g_hdr; static uint64_t g_frame = 1000;
static void setup(void) {
    size_t PG = (size_t)getpagesize();
    if (posix_memalign((void **)&g_pool, PG, 1 << 20)) abort(); memset(g_pool, 0, 1 << 20); mtl_pool_add(g_pool, 1 << 20);
    g_mat = g_pool; float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; memcpy(g_mat, m, 64);
    g_cb3 = g_pool + 4096; float l[16] = {1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1}; memcpy(g_cb3 + 336, l, 64);
    g_cam = g_pool + 8192; float c[16] = {0.5f, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 0.01f, 0, 0, 0, 0.5f, 1}; memcpy(g_cam + 336, c, 64);
    g_vtx = g_pool + 65536; for (int i = 0; i < 4096; i++) { int16_t *v = (int16_t *)(g_vtx + i * 8); v[0] = (int16_t)((i % 64) * 400); v[1] = (int16_t)((i / 64) * 400); v[2] = (int16_t)(i % 7 * 100); v[3] = 0; }
    g_idx = g_pool + 131072; for (uint32_t i = 0; i < 60000; i++) ((uint16_t *)g_idx)[i] = (uint16_t)(((i * 2654435761u) >> 13) % 4096);
    g_pr = [NvProg new]; g_pr->sh[0] = [NvShader new]; g_pr->sh[0]->rf.nb = 1; g_pr->sh[0]->rf.b[0] = (nvn_rbind){.kind = NVN_RB_CBUF, .reg = 3};
    g_sdepth = (tex_t){.serial = 79101}; g_sdepth.d.width = g_sdepth.d.height = 256; g_sdepth.d.depth = g_sdepth.d.samples = g_sdepth.d.levels = 1; g_sdepth.d.target = NVN_TEXTURE_TARGET_2D; g_sdepth.d.format = NVN_FORMAT_DEPTH32F;
    g_mdepth = (tex_t){.serial = 79102}; g_mdepth.d.width = 640; g_mdepth.d.height = 360; g_mdepth.d.depth = g_mdepth.d.samples = g_mdepth.d.levels = 1; g_mdepth.d.target = NVN_TEXTURE_TARGET_2D; g_mdepth.d.format = NVN_FORMAT_DEPTH32F_STENCIL8;
    g_hdr = g_mdepth; g_hdr.serial = 79103; g_hdr.d.format = NVN_FORMAT_R11G11B10F;
}
static void prepass(void) { memset(&R, 0, sizeof R); R.rt.depth = &g_mdepth; S.ubo[0][3] = (p_bind_buf){0, 3, (uint64_t)g_cam, 400}; p_draw d = {NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3}; rts_draw(g_pr, &d, NULL, NVN_DRAW_PRIMITIVE_TRIANGLES); }
// 游戏的阴影通道：阴影图写成 0.25（画过的影），按生产 rp_begin_ex 的位置登记清除值 1.0（全亮） | The game's shadow pass: the map becomes 0.25 (drawn shadows), registering the clear value 1.0 (fully lit) where production rp_begin_ex does
static void shadow_pass(void) {
    rp_end(); id<MTLTexture> t = tex_mtl(&g_sdepth);
    RPD *rp = [RPD new]; rp.depthAttachment.texture = t; rp.depthAttachment.loadAction = MTLLoadActionClear; rp.depthAttachment.clearDepth = 0.25; rp.depthAttachment.storeAction = MTLStoreActionStore;
    RENC en = [g_cb renderCommandEncoderWithDescriptor:rp]; [en barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice]; m4_end(en);
    memset(&R, 0, sizeof R); R.rt.depth = &g_sdepth; R.has_rt = 1;
    rp.depthAttachment.clearDepth = 1.0; rts_note_pass(rp);
}
// 一个遮挡体：第 i 个网格（48 个索引，各自的索引段） | One occluder: mesh i (48 indices, each with its own index span)
static uint8_t *g_lvp3;   // 阴影绘制用的光源常量（缺省 g_cb3）；局部回退那节用斜照的 | the light constants of shadow draws (g_cb3 by default); the partial-fallback section uses a slanted one
static void sh(int i) {
    memset(&R, 0, sizeof R); R.rt.depth = &g_sdepth; R.has_rt = 1;
    S.nattr = 1; S.attr[0] = (va_st){.fmt = 0x2b, .offset = 0, .stream = 0}; S.stream[0] = (vs_st){8, 0}; S.vbuf[0] = (NVNbufferRange){(uint64_t)g_vtx, 4096 * 8};
    S.ubo[0][0] = (p_bind_buf){0, 0, (uint64_t)g_mat, 64}; S.ubo[0][3] = (p_bind_buf){0, 3, (uint64_t)(g_lvp3 ? g_lvp3 : g_cb3), 400};
    p_draw_idx di = {NVN_DRAW_PRIMITIVE_TRIANGLES, 1, 48, 0, 0, 1, (uint64_t)(g_idx + (size_t)i * 96)};
    rts_draw(g_pr, NULL, &di, NVN_DRAW_PRIMITIVE_TRIANGLES);
}
// 一次进不了 TLAS 的阴影绘制（点图元，「图元不认」）：新网格现在当帧就进 TLAS，打断光追画面改用它 | one shadow draw that cannot enter the TLAS (points, "unknown primitive"): new meshes now enter the TLAS in the same frame, so this is what breaks the ray-traced picture
static void pt(void) {
    memset(&R, 0, sizeof R); R.rt.depth = &g_sdepth; R.has_rt = 1;
    S.nattr = 1; S.attr[0] = (va_st){.fmt = 0x2b, .offset = 0, .stream = 0}; S.stream[0] = (vs_st){8, 0}; S.vbuf[0] = (NVNbufferRange){(uint64_t)g_vtx, 4096 * 8};
    S.ubo[0][0] = (p_bind_buf){0, 0, (uint64_t)g_mat, 64}; S.ubo[0][3] = (p_bind_buf){0, 3, (uint64_t)(g_lvp3 ? g_lvp3 : g_cb3), 400};
    p_draw_idx di = {NVN_DRAW_PRIMITIVE_POINTS, 1, 48, 0, 0, 1, (uint64_t)g_idx};
    rts_draw(g_pr, NULL, &di, NVN_DRAW_PRIMITIVE_POINTS);
}
static float map_depth(void) {   // 回读阴影图中心的深度 | Read back the shadow map's centre depth
    id<MTLBuffer> b = [g_dev newBufferWithLength:256 * 256 * 4 options:MTLResourceStorageModeShared]; res_add(b);
    mtl_sub_begin(); [blit() copyFromTexture:tex_mtl(&g_sdepth) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(256, 256, 1) toBuffer:b destinationOffset:0 destinationBytesPerRow:256 * 4 destinationBytesPerImage:256 * 256 * 4];
    uint64_t f = mtl_sub_commit(); mtl_wait(f, 5000000000ull); float v = ((float *)b.contents)[128 * 256 + 128]; res_remove(b); return v;
}
// 一整帧：营地常驻的遮挡体 0–3，nw 个新出现的网格（当帧建好进 TLAS）各带一次进不了 TLAS 的绘制（→ 整帧照原版）；返回本帧画面是不是光追（阴影图清回 1.0） | One whole frame: the camp's resident occluders 0-3 plus nw newly appearing meshes (pending this frame → whole frame original); returns whether this frame showed ray tracing (map cleared back to 1.0)
static void set_rt(int n, const tex_t *c, const tex_t *d) { p_set_rt r = {0}; r.n = n; r.color[0] = c; r.depth = d; R.rt = r; R.has_rt = 1; rts_set_rt(&r); }
static int g_new = 100;
static int frame(int nw) {
    @autoreleasepool { mtl_set_frame(++g_frame); mtl_sub_begin(); prepass(); shadow_pass(); for (int i = 0; i < 4; i++) sh(i); for (int k = 0; k < nw; k++) { sh(g_new++); pt(); }
      set_rt(1, &g_hdr, &g_mdepth); set_rt(1, &g_hdr, NULL); uint64_t f = mtl_sub_commit(); mtl_wait(f, 10000000000ull); }
#ifdef RTS_PART_SET
    if (g_rts.part) return RTS_MAP_CLEARED() > 0;   // 局部回退：阴影图不清，画面是不是光追看本帧用没用上遮罩 | partial fallback: maps are not cleared; whether the frame showed ray tracing is whether it used the mask
#endif
    return map_depth() == 1.0f;
}
static int until_rt(int max) { for (int i = 1; i <= max; i++) if (frame(0)) return i; return 0; }   // 跑到光追画面，返回用了几帧（0 = 没到） | run until the ray-traced picture, returns how many frames it took (0 = never)
// 跑 n 帧「完整 / 有新网格」交替（偶数帧完整、奇数帧来一个新网格），返回画面切换次数（光追 ↔ 原版，含第一帧相对之前的切换） | Run n frames alternating complete / with a new mesh, returns the number of picture switches (ray traced <-> original, including the first frame against the one before)
static int alternate(int n, int prev, int *nrt) {
    int sw = 0; *nrt = 0; for (int k = 0; k < n; k++) { int s = frame(k & 1); sw += s != prev; prev = s; *nrt += s; } return sw;
}

int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); unsetenv("DM_RT_SHADOW"); unsetenv("DM_RT_SHADOW_HYST"); setenv("DM_RT_SHADOW_DBG", "1", 1); setenv("DM_RT_SHADOW_PART", "0", 1); mtl_init();   // 前面各节测「整帧清图」对照模式（DM_RT_SHADOW_PART=0）；生产缺省的局部回退另有几节（g_rts.part = 1） | the earlier sections test the clear-the-frame comparison mode (DM_RT_SHADOW_PART=0); the production default, partial fallback, has sections of its own (g_rts.part = 1)   // 测试里没有 _rt 变体：调试模式下用叠加合成（同 t_rts_gate） | no _rt variants in the test: debug mode composites with the overlay (as in t_rts_gate)
    if (!mtl_rt_shadow_supported()) { printf("SKIP：本机不支持光追阴影\n"); return 77; }
    setup(); rts_on(); g_rts.on = 1;
    int nrt;
    // 1. 起步：没中断过时照旧，上一帧完整这一帧就光追 | 1. Start: with no break yet, as before, a complete previous frame means ray tracing this frame
    EXPECT("起步：几帧内进入光追画面", 1, until_rt(12) > 0);
    // 2. 真机 d42c8ee 营地的形态：完整帧与「有新网格待建」帧交替 40 帧 | 2. The shape on the device in camp: complete frames alternating with "new mesh pending" frames for 40 frames
    // 模块在下一帧开头才记上一帧的切换：先多跑一帧光追，把进入光追那次切换记掉 | the module records a frame's switch at the next frame start: run one more ray-traced frame first so the switch into ray tracing is recorded
    EXPECT("交替前：这一帧仍是光追画面", 1, frame(0));
    { long long s0 = RTS_SWITCHES(); int sw = alternate(40, 1, &nrt);
      printf("交替 40 帧：画面切换 %d 次，其中光追帧 %d（切回原版后要连续完整 %d 帧）\n", sw, nrt, RTS_NEED());
      EXPECT("交替 40 帧：画面最多切换 1 次（光追 → 原版后不再来回切）", 1, sw <= 1);
      EXPECT("交替 40 帧：模块记下的画面切换次数与回读一致", sw, RTS_SWITCHES() - s0);
      EXPECT("等待期（阴影图没清）不绑遮罩：画面就是原版，不叠两层影（真机 79eaa3f 角色双影）", 1, ({ frame(1); !g_rts.map_cleared && rts_mask_tex() == g_rts.mask_lit && g_rts.mask_lit.width == g_mdepth.d.width; }));
      EXPECT("交替那几帧新网格照常建好（等待期不停建 BLAS）", 1, ({ int r = 0; for (uint32_t k = 0; k < g_rts.meshes.cap; k++) { rts_mesh *m = g_rts.meshes.vals[k]; if (m && m->state == RTS_READY) r++; } r >= 4 + 18; })); }
    // 3. 稳定之后回到光追：至少等满要求的连续完整帧数，但不会一直不回 | 3. After it settles, ray tracing comes back: not before the required streak of complete frames, but not never
    { int need = RTS_NEED(); int n = until_rt(200);
      printf("稳定后第 %d 帧回到光追（要求连续完整 %d 帧）\n", n, need);
      EXPECT("稳定后回到光追", 1, n > 0); EXPECT("回到光追前等满了连续完整的帧数（不是下一帧就切）", 1, n >= 30); }
    // 4. 再中断一次：要求翻倍 | 4. Another break: the requirement doubles
    { int need0 = RTS_NEED(); frame(1); int n = until_rt(400);
      printf("第二次中断：要求 %d → %d，第 %d 帧回到光追\n", need0, RTS_NEED(), n);
      EXPECT("第二次中断后要求翻倍", 1, RTS_NEED() == need0 * 2); EXPECT("第二次中断后等满翻倍的帧数才回光追", 1, n >= need0 * 2); }
    // 5. 原因不是光追本身失败：回退原版阴影图（退避）这条路不受影响，缺相机的当帧仍照原版 | 5. Failures of ray tracing itself still go the backoff path; a frame missing the camera stays original
    { int s; @autoreleasepool { mtl_set_frame(++g_frame); mtl_sub_begin(); shadow_pass(); for (int i = 0; i < 4; i++) sh(i); set_rt(1, &g_hdr, &g_mdepth); set_rt(1, &g_hdr, NULL); uint64_t f = mtl_sub_commit(); mtl_wait(f, 10000000000ull); } s = map_depth() == 1.0f;
      EXPECT("缺相机的当帧照原版", 0, s); }
    // 6. DM_RT_SHADOW_HYST=0（真机对照用）：回到旧行为，交替帧逐帧切换 | 6. DM_RT_SHADOW_HYST=0 (device A/B): back to the old behaviour, alternating frames switch every frame
    { RTS_HYST_SET(0); RTS_HYST_RESET(); until_rt(12); int sw = alternate(20, 1, &nrt);
      printf("迟滞关：交替 20 帧画面切换 %d 次\n", sw);
      EXPECT("迟滞关（对照）：交替帧逐帧切换", 1, sw >= 15); RTS_HYST_SET(1); }
#ifdef RTS_PART_SET
    // 7. 局部回退（生产缺省，七审三轮 P1 / P2）：同样每隔一帧冒出新网格 + 一次进不了 TLAS 的绘制，没进的照原版画进阴影图，画面一直是光追、一次都不切；
    //    光追本身失败（缺相机）照样切回原版、等满迟滞再回 | 7. Partial fallback (production default, seventh review round 3 P1 / P2): the same new mesh + one draw outside the TLAS every other frame; the uncovered draws go into the map as original, the picture stays ray traced and never switches;
    //    a failure of ray tracing itself (no camera) still switches back and waits out the hysteresis
    { static uint8_t *tilt; if (!tilt) { tilt = g_pool + 20480; float l[16] = {1, 0, 0, 0, 0, 0.6f, 0.8f, 0, 0, 0.8f, -0.6f, 0, 0, 0, 0, 1}; memcpy(tilt + 336, l, 64); }   // 斜照的太阳（竖直的在局部回退里不算太阳） | a slanted sun (a vertical light is not the sun under partial fallback)
      RTS_PART_SET(1); g_lvp3 = tilt; frame(0); RTS_HYST_RESET();   // 换模式的头一帧还在结算第 6 节最后那帧（对照模式、有中断），先跑掉再清迟滞要求 | the first frame after the switch still settles section 6's last frame (comparison mode, interrupted): run it first, then drop the hysteresis requirement
      EXPECT("局部回退：几帧内进入光追画面", 1, until_rt(12) > 0); frame(0);
      long long s0 = RTS_SWITCHES(); int sw = alternate(40, 1, &nrt);
      printf("局部回退 交替 40 帧：画面切换 %d 次，光追帧 %d\n", sw, nrt);
      EXPECT("局部回退：交替 40 帧画面一次都不切", 0, sw); EXPECT("局部回退：40 帧都是光追画面", 40, nrt); EXPECT("局部回退：模块也没记切换", 0, RTS_SWITCHES() - s0);
      int s; @autoreleasepool { mtl_set_frame(++g_frame); mtl_sub_begin(); shadow_pass(); for (int i = 0; i < 4; i++) sh(i); set_rt(1, &g_hdr, &g_mdepth); set_rt(1, &g_hdr, NULL); uint64_t f = mtl_sub_commit(); mtl_wait(f, 10000000000ull); } s = RTS_MAP_CLEARED() > 0;
      int n = until_rt(200); printf("局部回退 缺相机之后第 %d 帧回到光追（要求连续完整 %d 帧）\n", n, RTS_NEED());
      EXPECT("局部回退：缺相机的当帧不是光追画面", 0, s); EXPECT("局部回退：真失败后等满迟滞才回光追", 1, n >= 30); g_lvp3 = NULL; RTS_PART_SET(0); }
#endif
    printf("== %d 项，失败 %d ==\n", count, fail);
    return fail != 0;
}}
#endif
