// 光追阴影网格的生命周期与就绪闸门（五审 Q5-02、第 7.3 节）：生产 rts_draw / rts_set_rt / rts_build_and_trace / rts_frame_begin + 真 Metal 设备，每帧真提交、等完成、查命令缓冲错误。
// 场景：缺相机后恢复、缺深度、构建失败（建不出编码器）、关了再开、缓存淘汰、不支持的图元 / 格式回退原版绘制、光追帧失败后退回原版阴影图再恢复。
// 机器不支持光追（supportsRaytracing、Apple9）或旧接口版时跳过（退出 77）。
// Lifecycle of ray-traced shadow meshes and the readiness gate (fifth review Q5-02, section 7.3): production rts_draw / rts_set_rt / rts_build_and_trace / rts_frame_begin + a real Metal device; every frame is really submitted, waited for and checked for command buffer errors.
// Scenarios: recovery after a missing camera, missing depth, a failed build (no encoder), off then on again, cache eviction, unsupported primitives / formats falling back to the original draw, and a failed ray-traced frame falling back to the original shadow map and recovering.
// Skipped (exit 77) when the machine cannot ray trace (supportsRaytracing, Apple9) or in the older-API build.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
#if DM_NVN_M3
int main(void) { printf("SKIP：旧接口版没有光追阴影模块\n"); return 77; }
#else
#ifndef RTS_MAP_CLEARED
#define RTS_MAP_CLEARED_T() 0
#else
#define RTS_MAP_CLEARED_T() RTS_MAP_CLEARED()
#endif
#ifndef RTS_MESH_READY
#define RTS_MESH_READY(m) ((m)->built)   // 46e57ec 只有 built 一个标记：拿它当「可用」（对照旧代码用） | 46e57ec only has the built flag: used as "usable" (for running against the old code)
#endif
static int fail, count;
#define EXPECT(name, want, got) do { long long w_=(long long)(want), g_=(long long)(got); count++; printf("%s 期望 %lld → 真结果 %lld %s\n", name, w_, g_, w_==g_?"✓":"✗"); fail += w_ != g_; } while (0)
static uint8_t *g_pool, *g_mat, *g_cb3, *g_cam, *g_vtx, *g_idx;
static NvProg *g_pr; static tex_t g_sdepth, g_mdepth, g_hdr; static uint64_t g_frame = 1000; static int g_in_frame;
static void setup(void) {
    size_t PG = (size_t)getpagesize();
    if (posix_memalign((void **)&g_pool, PG, 1 << 20)) abort(); memset(g_pool, 0, 1 << 20); mtl_pool_add(g_pool, 1 << 20);
    g_mat = g_pool; float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; memcpy(g_mat, m, 64);
    g_cb3 = g_pool + 4096; float l[16] = {1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1}; memcpy(g_cb3 + 336, l, 64);   // 光源视投影：z 列朝上 = 太阳 | light view-projection: z column up = the sun
    g_cam = g_pool + 8192; float c[16] = {0.5f, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 0.01f, 0, 0, 0, 0.5f, 1}; memcpy(g_cam + 336, c, 64);   // 可逆的相机视投影 | an invertible camera view-projection
    g_vtx = g_pool + 65536; for (int i = 0; i < 4096; i++) { int16_t *v = (int16_t *)(g_vtx + i * 8); v[0] = (int16_t)((i % 64) * 400); v[1] = (int16_t)((i / 64) * 400); v[2] = (int16_t)(i % 7 * 100); v[3] = 0; }
    g_idx = g_pool + 131072; for (uint32_t i = 0; i < 3000; i++) ((uint16_t *)g_idx)[i] = (uint16_t)(((i * 2654435761u) >> 13) % 512);   // 伪随机、不成周期：各段内容互不相同，mesh_of 才不会认错 | pseudo-random, not periodic: segments differ so mesh_of never picks the wrong one
    g_pr = [NvProg new]; g_pr->sh[0] = [NvShader new]; g_pr->sh[0]->rf.nb = 1; g_pr->sh[0]->rf.b[0] = (nvn_rbind){.kind = NVN_RB_CBUF, .reg = 3};
    g_sdepth = (tex_t){.serial = 78001}; g_sdepth.d.width = g_sdepth.d.height = 256; g_sdepth.d.depth = g_sdepth.d.samples = g_sdepth.d.levels = 1; g_sdepth.d.target = NVN_TEXTURE_TARGET_2D; g_sdepth.d.format = NVN_FORMAT_DEPTH32F;
    g_mdepth = (tex_t){.serial = 78002}; g_mdepth.d.width = 640; g_mdepth.d.height = 360; g_mdepth.d.depth = g_mdepth.d.samples = g_mdepth.d.levels = 1; g_mdepth.d.target = NVN_TEXTURE_TARGET_2D; g_mdepth.d.format = NVN_FORMAT_DEPTH32F_STENCIL8;
    g_hdr = g_mdepth; g_hdr.serial = 78003; g_hdr.d.format = NVN_FORMAT_R11G11B10F;
}
// 每帧：开命令缓冲 →（各通道）→ 提交并等完成，命令缓冲不得报错 | Per frame: open the command buffer → (passes) → submit and wait; the command buffer must not report errors
static void frame_open(void) { mtl_set_frame(++g_frame); mtl_sub_begin(); g_in_frame = 1; }
static int frame_close(void) { uint64_t f = mtl_sub_commit(); g_in_frame = 0; int ok = mtl_wait(f, 5000000000ull); return ok && !mtl_last_commit_error(); }
static void prepass(void) {   // 深度预渲染的一次绘制：记相机 | One depth-prepass draw: notes the camera
    memset(&R, 0, sizeof R); R.rt.depth = &g_mdepth; S.ubo[0][3] = (p_bind_buf){0, 3, (uint64_t)g_cam, 400};
    p_draw d = {NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3}; rts_draw(g_pr, &d, NULL, NVN_DRAW_PRIMITIVE_TRIANGLES);
}
// 阴影通道的一次索引绘制：网格 i = 索引缓冲里第 i 段（每段 n 个索引） | One indexed shadow-pass draw: mesh i = segment i of the index buffer (n indices each)
static int sdraw(int i, int n, int prim, uint32_t fmt) {
    memset(&R, 0, sizeof R); R.rt.depth = &g_sdepth;
#ifdef RTS_HAS_NOTE_PASS
    if (g_rts.frame != atomic_load(&g_cur_frame) || g_rts.cur_smap < 0) {   // 本帧第一次画阴影：按生产 rp_begin_ex 的位置登记阴影通道（清除值 1.0） | first shadow draw this frame: register the shadow pass where production rp_begin_ex does (clear value 1.0)
        RPD *rp = [RPD new]; rp.depthAttachment.texture = tex_mtl(&g_sdepth); rp.depthAttachment.loadAction = MTLLoadActionClear; rp.depthAttachment.clearDepth = 1.0; rts_note_pass(rp); }
#endif
    S.nattr = 1; S.attr[0] = (va_st){.fmt = fmt, .offset = 0, .stream = 0}; S.stream[0] = (vs_st){8, 0}; S.vbuf[0] = (NVNbufferRange){(uint64_t)g_vtx, 4096 * 8};
    S.ubo[0][0] = (p_bind_buf){0, 0, (uint64_t)g_mat, 64}; S.ubo[0][3] = (p_bind_buf){0, 3, (uint64_t)g_cb3, 400};
    p_draw_idx di = {prim, 1, n, 0, 0, 1, (uint64_t)(g_idx + (size_t)i * 64 * 2)};
    return rts_draw(g_pr, NULL, &di, prim);
}
static int sh(int i) { return sdraw(i, 48, NVN_DRAW_PRIMITIVE_TRIANGLES, 0x2b); }
static void set_rt(int n, const tex_t *c, const tex_t *d) { p_set_rt r = {0}; r.n = n; r.color[0] = c; r.depth = d; R.rt = r; R.has_rt = 1; rts_set_rt(&r); }
static void main_pass(void) { set_rt(1, &g_hdr, &g_mdepth); }        // 主不透明通道：建 BLAS / TLAS、追踪 | Main opaque pass: build BLAS / TLAS, trace
static void hdr_again(void) { set_rt(1, &g_hdr, NULL); }             // 主通道之后再挂 HDR：第一阶段合成 | HDR attached again after the main pass: phase-1 compositing
// 返回本帧进了 TLAS 的阴影绘制数（六审之后阴影绘制一律照原版编码，「走光追」看这个） | Returns how many shadow draws entered the TLAS this frame (after the sixth review every shadow draw is still encoded; "ray traced" is measured by this)
static int full_frame(int cam, const int *ids, int nid, int *gpu_ok) {
    frame_open(); if (cam) prepass(); for (int k = 0; k < nid; k++) sh(ids[k]); int r = g_rts.ninst + g_rts.ndup; main_pass(); hdr_again(); *gpu_ok = frame_close(); return r;
}
static rts_mesh *mesh_of(int i) {   // 用同一次绘制的键找网格（不改状态：直接算键） | Find mesh i by recomputing the key of the same draw (no state change)
    for (uint32_t k = 0; k < g_rts.meshes.cap; k++) { rts_mesh *m = g_rts.meshes.vals[k]; if (m && m->ib && m->ib.length == 48 * 2 && !memcmp(m->ib.contents, g_idx + (size_t)i * 128, 96)) return m; }
    return NULL;
}

int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); unsetenv("DM_RT_SHADOW"); setenv("DM_RT_SHADOW_DBG", "1", 1); mtl_init();   // rt2：测试里没有 _rt 变体，调试模式下用叠加合成（审计分支「无变体 = 叠加」的同一路径） | rt2: no _rt variants in the test; debug mode composites with the overlay (the audit branch's "no variants = overlay" path)
    if (!mtl_rt_shadow_supported()) { printf("SKIP：本机不支持光追阴影\n"); return 77; }
    setup(); rts_on(); g_rts.on = 1;   // 相当于设置页开着光追阴影（g_rts.on 置 0 = 关） | as if the settings page had ray-traced shadows on (g_rts.on = 0 = off)
    int ok, r; int a[1] = {0};
    // 1. 新网格首帧：还没建成，不能进 TLAS，本次绘制照原版编码 | 1. First frame of a new mesh: not built yet, must not enter the TLAS, the draw is encoded as original
    r = full_frame(1, a, 1, &ok); EXPECT("新网格首帧：绘制照原版编码（返回 0）", 0, r); EXPECT("新网格首帧：TLAS 里没有未建成的实例", 0, g_rts.ninst); EXPECT("新网格首帧：GPU 无错", 1, ok);
    // 2. 缺相机后恢复：网格 1 首次出现那帧没有相机 | 2. Recovery after a missing camera: mesh 1 first appears in a frame without a camera
    int b[2] = {0, 1}; r = full_frame(0, b, 2, &ok); EXPECT("缺相机帧：GPU 无错", 1, ok);
    for (int i = 0; i < 3; i++) r = full_frame(1, b, 2, &ok);
    { rts_mesh *m = mesh_of(1); EXPECT("缺相机后恢复：网格 1 在缓存里", 1, m != NULL); EXPECT("缺相机后恢复：网格 1 最终可用", 1, m && RTS_MESH_READY(m)); }
    EXPECT("缺相机后恢复：两次绘制都走光追", 2, r); EXPECT("缺相机后恢复：本帧算了遮罩", 1, g_rts.mask_done); EXPECT("缺相机后恢复：遮罩合成进画面", 1, g_rts.overlay_done); EXPECT("缺相机后恢复：GPU 无错", 1, ok);
    // 3. 缺深度：网格 2 登记后主通道拿不到深度 | 3. Missing depth: mesh 2 is queued, then the main pass has no depth
    frame_open(); prepass(); sh(0); sh(2); rts_build_and_trace(nil); ok = frame_close(); EXPECT("缺深度帧：GPU 无错", 1, ok);
    int c[3] = {0, 1, 2}; for (int i = 0; i < 3; i++) r = full_frame(1, c, 3, &ok);
    { rts_mesh *m = mesh_of(2); EXPECT("缺深度后：网格 2 最终可用", 1, m && RTS_MESH_READY(m)); } EXPECT("缺深度后：三次绘制都走光追", 3, r); EXPECT("缺深度后：GPU 无错", 1, ok);
    // 4. 构建失败：网格 3 排队那帧建不出计算编码器（没有命令缓冲） | 4. Failed build: in mesh 3's frame no compute encoder can be created (no command buffer)
    mtl_set_frame(++g_frame); prepass(); sh(3); main_pass();
    { rts_mesh *m = mesh_of(3); EXPECT("构建失败：网格 3 留在缓存", 1, m != NULL); EXPECT("构建失败：网格 3 不得标成可用", 0, m && RTS_MESH_READY(m)); }
    int d4[4] = {0, 1, 2, 3}; for (int i = 0; i < 3; i++) r = full_frame(1, d4, 4, &ok);
    { rts_mesh *m = mesh_of(3); EXPECT("构建失败后：网格 3 重新排队并建成", 1, m && RTS_MESH_READY(m)); } EXPECT("构建失败后：四次绘制都走光追", 4, r); EXPECT("构建失败后：GPU 无错", 1, ok);
    // 5. 关了再开：网格 4 排队后（本帧没到主通道）关掉，几帧后再开，那帧只画网格 5 | 5. Off then on: mesh 4 is queued (no main pass this frame), switched off, on again a few frames later drawing only mesh 5
    frame_open(); prepass(); sh(4); g_rts.on = 0; ok = frame_close();
    for (int i = 0; i < 5; i++) { frame_open(); EXPECT("关着：绘制照原版", 0, sh(4)); frame_close(); }
    g_rts.on = 1; frame_open(); prepass(); sh(5); r = g_rts.ninst;
    EXPECT("再开首帧：排队里只有本帧的网格 5（不带关之前的过期源地址）", 1, g_rts.npend); EXPECT("再开首帧：上一帧不是光追帧，本帧没有实例", 0, r);
    main_pass(); hdr_again(); ok = frame_close(); EXPECT("再开首帧：GPU 无错", 1, ok);
    int e6[6] = {0, 1, 2, 3, 4, 5}; for (int i = 0; i < 3; i++) r = full_frame(1, e6, 6, &ok);
    EXPECT("再开后：六次绘制都走光追", 6, r); EXPECT("再开后：GPU 无错", 1, ok);
    // 6. 不支持的图元 / 格式：照原版画（不能只丢掉） | 6. Unsupported primitive / format: drawn as original (not just dropped)
    frame_open(); prepass(); sdraw(0, 48, NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP, 0x2b); sdraw(6, 48, NVN_DRAW_PRIMITIVE_TRIANGLES, 0x30); sdraw(7, 2, NVN_DRAW_PRIMITIVE_TRIANGLES, 0x2b);
    EXPECT("三角带 / 格式不认 / 不足 3 个索引：都不进 TLAS", 0, g_rts.ninst); EXPECT("三角带 / 格式不认 / 不足 3 个索引：本帧阴影图不清（照原版）", 0, RTS_MAP_CLEARED_T()); main_pass(); hdr_again(); frame_close();
    // 7. 光追帧失败（本帧缺相机，遮罩算不出）→ 下一帧退回原版阴影图 → 原版帧条件齐全后恢复光追 | 7. A ray-traced frame fails (no camera, no mask) → the next frame uses the original shadow map → ray tracing resumes once a frame has everything
    r = full_frame(1, a, 1, &ok); EXPECT("失败前：光追帧", 1, r);
    r = full_frame(0, a, 1, &ok); EXPECT("缺相机的光追帧：遮罩没算", 0, g_rts.mask_done);
    r = full_frame(1, a, 1, &ok); EXPECT("失败后下一帧：退回原版阴影图（返回 0）", 0, r);
    int back = 0; for (int i = 0; i < 8 && !back; i++) back = full_frame(1, a, 1, &ok) == 1; EXPECT("条件恢复后几帧内回到光追", 1, back); EXPECT("恢复后：GPU 无错", 1, ok);
    // 8. 缓存淘汰：从没建成的网格 600 帧没用也要释放 | 8. Cache eviction: a mesh never built is released too after 600 unused frames
    frame_open(); sh(40); g_rts.on = 0; frame_close(); g_rts.on = 1;   // 网格 40 排队后这帧没到主通道 | mesh 40 queued, no main pass this frame
    { rts_mesh *m = mesh_of(40); EXPECT("网格 40 在缓存且没建成", 1, m && !RTS_MESH_READY(m)); }
    int before = g_rts.nmesh; for (int i = 0; i < 1300; i++) rts_frame_begin(++g_frame);
    EXPECT("1300 帧后未建成的网格 40 被淘汰", 1, mesh_of(40) == NULL); EXPECT("1300 帧后缓存清空", 0, g_rts.nmesh); (void)before;
    printf("== %d 项，失败 %d ==\n", count, fail);
    return fail != 0;
}}
#endif
