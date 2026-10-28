// 光追阴影六审剩余风险（第 5 节）：失败的当帧也要有影子、每帧常量 / 实例槽的 GPU 生命周期、BLAS 缓存总字节预算与关闭即释放。
// 生产 rts_draw / rts_set_rt / rts_build_and_trace / rts_frame_begin + 真 Metal 设备；阴影图是真贴图，每帧真提交、等完成、回读。
// 测试里的「游戏阴影通道」：一趟渲染通道把阴影图写成 0.25（代表画过的阴影），并按生产 rp_begin_ex 的位置登记本通道的清除值 1.0（全亮）。
// 光追成功 = 主通道之前阴影图被清回 1.0；失败 = 阴影图保持 0.25（原版阴影还在）。
// Remaining risks of the sixth review (section 5): the failing frame itself must keep its shadows, GPU lifetime of the per-frame constant / instance slots, a total byte budget for the BLAS cache and release on switch-off.
// Production rts_draw / rts_set_rt / rts_build_and_trace / rts_frame_begin + a real Metal device; the shadow map is a real texture, every frame is really submitted, waited for and read back.
// The test's "game shadow pass": one render pass writes the shadow map as 0.25 (standing for drawn shadows) and registers the pass's clear value 1.0 (fully lit) where production rp_begin_ex does.
// Ray tracing succeeded = the shadow map is cleared back to 1.0 before the main pass; failed = the shadow map stays 0.25 (original shadows kept).
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
#if DM_NVN_M3
int main(void) { printf("SKIP：旧接口版没有光追阴影模块\n"); return 77; }
#else
#ifndef RTS_LAST_SLOT_BUF
#define RTS_LAST_SLOT_BUF() (g_rts.instbuf[(g_rts.ri + 2) % 3])   // 79d571f：三槽轮转，刚用过的那一槽 | 79d571f: three rotating slots, the one just used
#endif
#ifndef RTS_HYST_RESET
#define RTS_HYST_RESET() ((void)0)   // 没有画面迟滞的旧代码 | older code without picture hysteresis
#endif
#ifndef RTS_MAP_CLEARED
#define RTS_MAP_CLEARED() 0   // 79d571f 从不清阴影图（靠不画） | 79d571f never clears the shadow map (it skips the draws instead)
#endif
#ifndef RTS_UNC
#define RTS_UNC(r) (-1LL)   // 3fc0f41 及更早：没进 TLAS 的绘制不分原因 | 3fc0f41 and earlier: draws outside the TLAS are not split by reason
#define RTS_UNC_PASS 0
#define RTS_UNC_QUOTA 0
#define RTS_UNC_PENDING 0
#define RTS_UNC_PRIM 0
#endif
#ifndef RTS_WHY
#define RTS_WHY() ""   // 4978d39 及更早：不记跳过追踪的具体原因 | 4978d39 and earlier: the specific reason for skipping the trace is not recorded
#endif
static int fail, count;
#define EXPECT(name, want, got) do { long long w_=(long long)(want), g_=(long long)(got); count++; printf("%s 期望 %lld → 真结果 %lld %s\n", name, w_, g_, w_==g_?"✓":"✗"); fail += w_ != g_; } while (0)
static uint8_t *g_pool, *g_mat, *g_cb3, *g_cam, *g_vtx, *g_idx, *g_big;
static NvProg *g_pr; static tex_t g_sdepth, g_mdepth, g_hdr; static uint64_t g_frame = 1000;
static void setup(void) {
    size_t PG = (size_t)getpagesize();
    if (posix_memalign((void **)&g_pool, PG, 1 << 20)) abort(); memset(g_pool, 0, 1 << 20); mtl_pool_add(g_pool, 1 << 20);
    g_mat = g_pool; float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; memcpy(g_mat, m, 64);
    g_cb3 = g_pool + 4096; float l[16] = {1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1}; memcpy(g_cb3 + 336, l, 64);
    g_cam = g_pool + 8192; float c[16] = {0.5f, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 0.01f, 0, 0, 0, 0.5f, 1}; memcpy(g_cam + 336, c, 64);
    g_vtx = g_pool + 65536; for (int i = 0; i < 4096; i++) { int16_t *v = (int16_t *)(g_vtx + i * 8); v[0] = (int16_t)((i % 64) * 400); v[1] = (int16_t)((i / 64) * 400); v[2] = (int16_t)(i % 7 * 100); v[3] = 0; }
    g_idx = g_pool + 131072; for (uint32_t i = 0; i < 3000; i++) ((uint16_t *)g_idx)[i] = (uint16_t)(((i * 2654435761u) >> 13) % 512);
    g_big = g_pool + 262144; for (uint32_t i = 0; i < 36 * 3000; i++) ((uint16_t *)g_big)[i] = (uint16_t)(((i * 2246822519u) >> 11) % 4096);
    g_pr = [NvProg new]; g_pr->sh[0] = [NvShader new]; g_pr->sh[0]->rf.nb = 1; g_pr->sh[0]->rf.b[0] = (nvn_rbind){.kind = NVN_RB_CBUF, .reg = 3};
    g_sdepth = (tex_t){.serial = 79001}; g_sdepth.d.width = g_sdepth.d.height = 256; g_sdepth.d.depth = g_sdepth.d.samples = g_sdepth.d.levels = 1; g_sdepth.d.target = NVN_TEXTURE_TARGET_2D; g_sdepth.d.format = NVN_FORMAT_DEPTH32F;
    g_mdepth = (tex_t){.serial = 79002}; g_mdepth.d.width = 640; g_mdepth.d.height = 360; g_mdepth.d.depth = g_mdepth.d.samples = g_mdepth.d.levels = 1; g_mdepth.d.target = NVN_TEXTURE_TARGET_2D; g_mdepth.d.format = NVN_FORMAT_DEPTH32F_STENCIL8;
    g_hdr = g_mdepth; g_hdr.serial = 79003; g_hdr.d.format = NVN_FORMAT_R11G11B10F;
}
static void frame_open(void) { mtl_set_frame(++g_frame); mtl_sub_begin(); }
static int frame_close(void) { uint64_t f = mtl_sub_commit(); int ok = mtl_wait(f, 10000000000ull); return ok && !mtl_last_commit_error(); }
static void prepass(void) { memset(&R, 0, sizeof R); R.rt.depth = &g_mdepth; S.ubo[0][3] = (p_bind_buf){0, 3, (uint64_t)g_cam, 400}; p_draw d = {NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3}; rts_draw(g_pr, &d, NULL, NVN_DRAW_PRIMITIVE_TRIANGLES); }
// 游戏的阴影通道开头：阴影图内容写成 0.25；生产里 rp_begin_ex 登记通道（清除值 1.0）。没有命令缓冲时只登记 | Start of the game's shadow pass: the map's content becomes 0.25; production rp_begin_ex registers the pass (clear value 1.0). Without a command buffer it only registers
static void shadow_pass(void) {
    rp_end(); id<MTLTexture> t = tex_mtl(&g_sdepth);
    RPD *rp = [RPD new]; rp.depthAttachment.texture = t; rp.depthAttachment.loadAction = MTLLoadActionClear; rp.depthAttachment.clearDepth = 0.25; rp.depthAttachment.storeAction = MTLStoreActionStore;
    if (g_cb) { RENC en = [g_cb renderCommandEncoderWithDescriptor:rp]; [en barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice]; m4_end(en); }
    memset(&R, 0, sizeof R); R.rt.depth = &g_sdepth; R.has_rt = 1;
#ifdef RTS_HAS_NOTE_PASS
    rp.depthAttachment.clearDepth = 1.0; rts_note_pass(rp);   // 游戏的清除值 1.0 = 全亮 | the game's clear value 1.0 = fully lit
#endif
}
static int sdraw(const uint8_t *ib, int n, int prim, uint32_t fmt) {
    memset(&R, 0, sizeof R); R.rt.depth = &g_sdepth; R.has_rt = 1;
    S.nattr = 1; S.attr[0] = (va_st){.fmt = fmt, .offset = 0, .stream = 0}; S.stream[0] = (vs_st){8, 0}; S.vbuf[0] = (NVNbufferRange){(uint64_t)g_vtx, 4096 * 8};
    S.ubo[0][0] = (p_bind_buf){0, 0, (uint64_t)g_mat, 64}; S.ubo[0][3] = (p_bind_buf){0, 3, (uint64_t)g_cb3, 400};
    p_draw_idx di = {prim, 1, n, 0, 0, 1, (uint64_t)ib};
    return rts_draw(g_pr, NULL, &di, prim);
}
static int sh(int i) { return sdraw(g_idx + (size_t)i * 128, 48, NVN_DRAW_PRIMITIVE_TRIANGLES, 0x2b); }
static int big(int i) { return sdraw(g_big + (size_t)i * 6000, 3000, NVN_DRAW_PRIMITIVE_TRIANGLES, 0x2b); }
static void set_rt(int n, const tex_t *c, const tex_t *d) { p_set_rt r = {0}; r.n = n; r.color[0] = c; r.depth = d; R.rt = r; R.has_rt = 1; rts_set_rt(&r); }
static void main_pass(void) { set_rt(1, &g_hdr, &g_mdepth); }
static void hdr_again(void) { set_rt(1, &g_hdr, NULL); }
static int covered(void) { return g_rts.ninst + g_rts.ndup; }
// 一帧：预渲染（可缺相机）→ 阴影通道（ids 里的网格）→ 主通道 → HDR；返回照原版编码的阴影绘制数（rts_draw 返回 0） | One frame; returns how many shadow draws were encoded as original (rts_draw returned 0)
static int run_frame(int cam, const int *ids, int nid, int *gpu_ok) {
    frame_open(); if (cam) prepass(); shadow_pass(); int enc = 0; for (int k = 0; k < nid; k++) enc += sh(ids[k]) == 0; main_pass(); hdr_again(); *gpu_ok = frame_close(); return enc;
}
static float map_depth(void) {   // 回读阴影图中心的深度 | Read back the shadow map's centre depth
    id<MTLBuffer> b = [g_dev newBufferWithLength:256 * 256 * 4 options:MTLResourceStorageModeShared]; res_add(b);
    mtl_sub_begin(); [blit() copyFromTexture:tex_mtl(&g_sdepth) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(256, 256, 1) toBuffer:b destinationOffset:0 destinationBytesPerRow:256 * 4 destinationBytesPerImage:256 * 256 * 4];
    uint64_t f = mtl_sub_commit(); mtl_wait(f, 5000000000ull); float v = ((float *)b.contents)[128 * 256 + 128]; res_remove(b); return v;
}
static float tex_depth(const tex_t *tx) {   // 回读任一张 256x256 深度图中心的值 | Read back the centre value of any 256x256 depth map
    id<MTLBuffer> b = [g_dev newBufferWithLength:256 * 256 * 4 options:MTLResourceStorageModeShared]; res_add(b);
    mtl_sub_begin(); [blit() copyFromTexture:tex_mtl(tx) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(256, 256, 1) toBuffer:b destinationOffset:0 destinationBytesPerRow:256 * 4 destinationBytesPerImage:256 * 256 * 4];
    uint64_t f = mtl_sub_commit(); mtl_wait(f, 5000000000ull); float v = ((float *)b.contents)[128 * 256 + 128]; res_remove(b); return v;
}
static int until_applied(const int *ids, int nid) {   // 跑完整帧直到光追生效（阴影图被清回 1.0），最多 12 帧 | Run whole frames until ray tracing applies (map cleared back to 1.0), at most 12
    // 这里各段测的是单次失败后恢复：每帧先清掉画面迟滞要求（切回原版是在下一帧开头才记的；迟滞本身见 t_rts_flicker） | the sections here test recovery from a single failure: drop the picture-hysteresis requirement before every frame (the switch back is recorded at the next frame start; the hysteresis itself is in t_rts_flicker)
    int ok; for (int i = 0; i < 12; i++) { RTS_HYST_RESET(); run_frame(1, ids, nid, &ok); if (map_depth() == 1.0f) return 1; } return 0;
}
static rts_mesh *mesh_by_ib(const uint8_t *ib, size_t len) {
    for (uint32_t k = 0; k < g_rts.meshes.cap; k++) { rts_mesh *m = g_rts.meshes.vals[k]; if (m && m->ib && m->ib.length == len && !memcmp(m->ib.contents, ib, len)) return m; }
    return NULL;
}
static uint64_t cache_bytes(void) {
    uint64_t t = 0; for (uint32_t k = 0; k < g_rts.meshes.cap; k++) { rts_mesh *m = g_rts.meshes.vals[k]; if (m) t += m->as.allocatedSize + m->pos.length + m->ib.length; } return t;
}
// 让 GPU 在这一帧开头空转约半秒：之后几帧的 CPU 编码一定跑在它前面 | Keep the GPU busy for about half a second at the start of this frame: the CPU encoding of the next few frames is then certainly ahead of it
static void gpu_spin(void) {
    static id<MTLComputePipelineState> ps; static id<MTL4ArgumentTable> at; static id<MTLBuffer> buf;
    if (!ps) { NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal; kernel void spin(device uint *p [[buffer(0)]], uint i [[thread_position_in_grid]]) { uint a = p[0] + i; for (uint k = 0; k < 300000000u; k++) a = a * 1664525u + 1013904223u; p[1] = a; }\n" options:nil error:&e];
        ps = [g_dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"spin"] error:&e]; MTL4ArgumentTableDescriptor *ad = [MTL4ArgumentTableDescriptor new]; ad.maxBufferBindCount = 1; at = [g_dev newArgumentTableWithDescriptor:ad error:&e];
        buf = [g_dev newBufferWithLength:16 options:MTLResourceStorageModeShared]; res_add(buf); }
    rp_end(); CENC ce = (CENC)enc_c_begin(NULL); [ce setArgumentTable:at]; enc_c_pso(ce, ps); [at setAddress:buf.gpuAddress atIndex:0]; enc_c_dispatch(ce, MTLSizeMake(1, 1, 1), MTLSizeMake(1, 1, 1)); enc_c_end(ce); args_dirty_all();
}

int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); unsetenv("DM_RT_SHADOW"); setenv("DM_RT_SHADOW_CACHE_MB", "1", 1); setenv("DM_RT_SHADOW_DBG", "1", 1); mtl_init();   // rt2：测试里没有 _rt 变体，调试模式下用叠加合成（审计分支「无变体 = 叠加」的同一路径） | rt2: no _rt variants in the test; debug mode composites with the overlay (the audit branch's "no variants = overlay" path)
    if (!mtl_rt_shadow_supported()) { printf("SKIP：本机不支持光追阴影\n"); return 77; }
    setup(); rts_on(); g_rts.on = 1;
    int ok, enc; int a[1] = {0};
    // 前面各节在自己的自动释放池里跑：生产里每帧都包在提交池里；测试里没开命令缓冲的那几帧产生的自动释放对象不能留到最外层，否则「关掉后释放」核不出来
    // The earlier sections run in their own autorelease pool: production wraps every frame in the submission's pool; autoreleased objects from the test's frames without a command buffer must not stay in the outermost pool, or "released after switch-off" cannot be checked
    @autoreleasepool {
    // 1. 失败的当帧：阴影图绘制照原版编码，阴影图不被清（原版阴影还在） | 1. The failing frame itself: shadow-map draws are encoded as original and the map is not cleared
    EXPECT("能进入光追（追踪成功后阴影图清回全亮）", 1, until_applied(a, 1));
    enc = run_frame(1, a, 1, &ok); EXPECT("光追成功帧：阴影绘制照常编码（阴影图兜底）", 1, enc); EXPECT("光追成功帧：网格进了 TLAS", 1, covered()); EXPECT("光追成功帧：阴影图清回 1.0", 1, map_depth() == 1.0f); EXPECT("光追成功帧：遮罩合成了", 1, g_rts.overlay_done); EXPECT("光追成功帧：GPU 无错", 1, ok);
    enc = run_frame(0, a, 1, &ok); EXPECT("缺相机的当帧：原版阴影绘制数 > 0", 1, enc > 0); EXPECT("缺相机的当帧：阴影图保持原版（0.25）", 1, map_depth() == 0.25f); EXPECT("缺相机的当帧：没有合成遮罩", 0, g_rts.overlay_done);
    EXPECT("缺相机后恢复光追", 1, until_applied(a, 1));
    frame_open(); prepass(); shadow_pass(); enc = sh(0) == 0; rts_build_and_trace(nil); hdr_again(); ok = frame_close();
    EXPECT("缺深度的当帧：原版阴影绘制数 > 0", 1, enc > 0); EXPECT("缺深度的当帧：阴影图保持原版（0.25）", 1, map_depth() == 0.25f); EXPECT("缺深度的当帧：没有合成遮罩", 0, g_rts.overlay_done);
    EXPECT("缺深度后恢复光追", 1, until_applied(a, 1));
    mtl_set_frame(++g_frame); prepass(); shadow_pass(); enc = sh(0) == 0; main_pass();   // 没有命令缓冲：建不出计算编码器 | no command buffer: no compute encoder
    EXPECT("编码失败的当帧：原版阴影绘制数 > 0", 1, enc > 0); EXPECT("编码失败的当帧：阴影图没被清", 0, RTS_MAP_CLEARED());
    EXPECT("编码失败后恢复光追", 1, until_applied(a, 1));
    { int b2[2] = {0, 1}; enc = run_frame(1, b2, 2, &ok);   // 网格 1 第一次出现、还没建成：本帧有阴影绘制进不了 TLAS | mesh 1 appears for the first time, not built: a shadow draw this frame cannot enter the TLAS
      EXPECT("有新网格的当帧：两次阴影绘制都照原版编码", 2, enc); EXPECT("有新网格的当帧：阴影图保持原版（不清，免得新网格没影）", 1, map_depth() == 0.25f); EXPECT("有新网格的当帧：没有合成遮罩（不叠两层影）", 0, g_rts.overlay_done);
      EXPECT("新网格建成后两者都进光追", 1, until_applied(b2, 2)); EXPECT("新网格建成后：两次绘制都进了 TLAS", 2, covered()); }
    // 2. 每帧常量 / 实例槽：GPU 还在跑第 N 帧时，之后几帧不得复写第 N 帧用的槽 | 2. Per-frame constant / instance slots: while the GPU is still on frame N, later frames must not overwrite frame N's slot
    { EXPECT("槽测试前光追生效", 1, until_applied(a, 1)); float *M = (float *)g_mat; uint64_t fN = 0, last = 0; uint8_t snap[64]; __unsafe_unretained id<MTLBuffer> slotN = nil;
      for (int k = 0; k < 4; k++) {
          frame_open(); if (k == 0) gpu_spin(); prepass(); shadow_pass(); M[12] = (float)(k + 1) * 10.f;   /* 平移：每帧实例描述都不同 | translation: the instance descriptor differs every frame */ sh(0); main_pass(); hdr_again(); last = mtl_sub_commit();
          if (k == 0) { fN = last; slotN = RTS_LAST_SLOT_BUF(); memcpy(snap, slotN.contents, 64); }
      }
      int busy = g_done < fN; EXPECT("第 N 帧的 GPU 工作还没完成（测试前提）", 1, busy);
      EXPECT("第 N 帧用的槽在 GPU 用完之前没被复写", 0, memcmp(slotN.contents, snap, 64) != 0);
      mtl_wait(last, 10000000000ull); M[12] = 0; EXPECT("槽测试后光追照常", 1, until_applied(a, 1)); }
    }
    // 3. 关掉即整体释放（等 GPU 在途工作完成）；再开能重建 | 3. Switching off releases everything (after in-flight GPU work); switching on again rebuilds
    { __weak id w_as = nil, w_tlas = nil, w_mask = nil; uint64_t builds0;
      @autoreleasepool { rts_mesh *m = mesh_by_ib(g_idx, 96); w_as = m ? m->as : nil; w_tlas = g_rts.tlas; w_mask = g_rts.mask; }
      int live0; @autoreleasepool { live0 = w_as && w_tlas && w_mask; }   // 读弱引用会把对象放进当前自动释放池：每次都在自己的池里读 | reading a weak reference puts the object into the current autorelease pool: always read inside a pool of its own
      EXPECT("关之前缓存非空", 1, g_rts.nmesh > 0 && live0);
      g_rts.on = 0;
      for (int i = 0; i < 4; i++) @autoreleasepool { frame_open(); sh(0); frame_close(); }
      int g_as = 0, g_tl = 0, g_mk = 0;
      for (int i = 0; i < 100 && !(g_as && g_tl && g_mk); i++) {   // GPU 做完之后 Metal 内部的引用是异步放掉的：最多等 2 秒（每 20 ms 空跑一帧让延迟释放表走一遍） | Metal drops its internal references asynchronously after the GPU finishes: wait up to 2 s (an empty frame every 20 ms runs the deferred-release table)
          @autoreleasepool { frame_open(); frame_close(); g_as = w_as == nil; g_tl = w_tlas == nil; g_mk = w_mask == nil; } if (!(g_as && g_tl && g_mk)) usleep(20000); }
      EXPECT("关掉后网格缓存清空", 0, g_rts.nmesh); EXPECT("关掉后 BLAS 已释放", 1, g_as); EXPECT("关掉后 TLAS 已释放", 1, g_tl); EXPECT("关掉后遮罩已释放", 1, g_mk);
      builds0 = g_rts.n_builds; g_rts.on = 1;
      EXPECT("再开后光追重建并生效", 1, until_applied(a, 1)); EXPECT("再开后重新建了 BLAS", 1, g_rts.n_builds > builds0); }
    // 4. 总字节预算 1 MB（DM_RT_SHADOW_CACHE_MB=1）：每帧 6 个大网格、共 36 个；超了按最久没用的淘汰 | 4. Total byte budget of 1 MB: 6 large meshes per frame, 36 in all; over budget evicts least recently used first
    { uint64_t peak = 0;
      for (int f = 0; f < 6; f++) { frame_open(); prepass(); shadow_pass(); for (int i = 0; i < 6; i++) big(f * 6 + i); main_pass(); hdr_again(); frame_close(); uint64_t b = cache_bytes(); if (b > peak) peak = b; }
      for (int f = 0; f < 2; f++) { frame_open(); prepass(); shadow_pass(); for (int i = 34; i < 36; i++) big(i); main_pass(); hdr_again(); frame_close(); }
      uint64_t b = cache_bytes(); printf("缓存字节：峰值 %.2f MB，现在 %.2f MB（%d 个网格）\n", peak / 1048576.0, b / 1048576.0, g_rts.nmesh);
      EXPECT("帧开始时缓存不超过 1 MB 预算", 1, b <= (1u << 20)); EXPECT("最久没用的大网格 0 被淘汰", 1, mesh_by_ib(g_big, 6000) == NULL); EXPECT("最近用的大网格 35 还在", 1, mesh_by_ib(g_big + 35 * 6000, 6000) != NULL);
      EXPECT("预算淘汰后光追照常", 1, until_applied(a, 1)); }
    // 5. 真机 4978d39 局内的形态：阴影图第一次挂上的通道是读入（生产 rp_begin_ex 开通道、R.clear_d = 0）。
    //    A：本帧先由游戏的 ClearTexture（生产 clear_tex）清成 1.0 → 光追要能生效；B：本帧没清（缓存的级联）→ 照原版，回退原因要写明「读入」
    // 5. The in-game shape on the device with 4978d39: the first pass on the shadow map opens with a load (production rp_begin_ex, R.clear_d = 0).
    //    A: the game's ClearTexture (production clear_tex) cleared it to 1.0 earlier this frame → ray tracing must apply; B: not cleared this frame (cached cascade) → original, and the fallback reason must say "load"
    { int ok2 = 0;
      for (int pass = 0; pass < 2; pass++) {
        int applied = 0; char why[256] = "";
        for (int i = 0; i < 12 && !applied; i++) { RTS_HYST_RESET();   // 画面迟滞另测（t_rts_flicker） | picture hysteresis is tested separately (t_rts_flicker)
            frame_open(); prepass(); rp_end();
            if (pass == 0) { p_clear_tex ct = {0}; ct.tex = &g_sdepth; ct.r.width = ct.r.height = 256; ct.r.depth = 1; ct.mask = 0xf; ct.c.f[0] = 1.0f; clear_tex(&ct); }
            { RPD *rp = [RPD new]; rp.depthAttachment.texture = tex_mtl(&g_sdepth); rp.depthAttachment.loadAction = MTLLoadActionClear; rp.depthAttachment.clearDepth = 0.25; rp.depthAttachment.storeAction = MTLStoreActionStore;
              RENC en = [g_cb renderCommandEncoderWithDescriptor:rp]; [en barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice]; m4_end(en); }   // 代表画上去的影（不经 rp_begin_ex，不登记） | stands for the rasterized shadows (not through rp_begin_ex, not registered)
            memset(&R, 0, sizeof R); R.rt.depth = &g_sdepth; R.has_rt = 1; rp_begin_ex(0);   // 生产开通道：读入 | production pass begin: load
            sh(0); main_pass(); hdr_again(); ok2 = frame_close();
            const char *w = RTS_WHY(); if (w && *w) snprintf(why, sizeof why, "%s", w);   // 刚结束这一帧的回退原因（生产在下一帧开头记同一句） | the fallback reason of the frame just finished (production logs the same text at the next frame's start)
            applied = map_depth() == 1.0f;
        }
        if (pass == 0) { EXPECT("ClearTexture 清过、通道读入：光追生效（阴影图清回 1.0）", 1, applied); EXPECT("ClearTexture 那几帧 GPU 无错", 1, ok2); }
        else { printf("没清的读入通道：回退原因「%s」\n", why); EXPECT("本帧没清、通道读入：不清阴影图（原版阴影保留）", 0, applied); EXPECT("回退原因写明是读入", 1, strstr(why, "读入") != NULL); }
      }
      // 退避中的帧（不是光追帧）不算网格键：真机键摘要每帧上毫秒，退避帧白算 | Frames in backoff (not ray-traced) compute no mesh key: on the device the key digest costs over a millisecond per frame, wasted in backoff
      // 按张处理之后 B 不再算失败（光追照常、只是那张不清），退避改用缺相机的一帧造出来 | with per-map handling B is no longer a failure (tracing applies, that map is just not cleared); backoff is produced by a frame without a camera instead
      int b1[2] = {1, 2}; run_frame(0, b1, 2, &ok); uint64_t k0 = g_rts.key_ns; run_frame(1, b1, 2, &ok);
      EXPECT("测试前提：这帧在退避中", 0, g_rts.rt_frame); EXPECT("退避帧不算网格键", 0, (long long)(g_rts.key_ns - k0)); }
    // 6. 光追帧里没进 TLAS 的绘制要分原因计数（真机 3fc0f41：走到某处后每帧约 8 次没进 TLAS，日志分不出原因）：
    //    一帧 14 个新网格 → 12 个排队（待建）、2 个配额满；下一帧阴影绘制所在的通道没登记 → 通道没登记
    // 6. Draws outside the TLAS in ray-traced frames are counted by reason (device run of 3fc0f41: about 8 per frame after walking somewhere, and the log could not tell why):
    //    14 new meshes in one frame → 12 queued (pending), 2 over the quota; next frame a shadow draw in an unregistered pass → pass not registered
    { EXPECT("分原因前光追生效", 1, until_applied(a, 1));
      long long q0 = RTS_UNC(RTS_UNC_QUOTA), p0 = RTS_UNC(RTS_UNC_PENDING), s0 = RTS_UNC(RTS_UNC_PASS);
      frame_open(); prepass(); shadow_pass(); for (int i = 10; i < 24; i++) sh(i); main_pass(); hdr_again(); frame_close();
      EXPECT("新建配额满：记 2 次", 2, RTS_UNC(RTS_UNC_QUOTA) - q0); EXPECT("网格待建：记 12 次", 12, RTS_UNC(RTS_UNC_PENDING) - p0);
      frame_open(); prepass(); rp_end(); EXPECT("测试前提：这帧是光追帧", 1, (sh(0), g_rts.rt_frame)); main_pass(); hdr_again(); frame_close();
      EXPECT("阴影通道没登记：记 1 次", 1, RTS_UNC(RTS_UNC_PASS) - s0); }
    // 7. 真机 d42c8ee 局内每帧 4–8 次「非三角形列表或不足 3 顶点」：不足 3 顶点的绘制画不出东西，算已覆盖；三角形带（含重启）展开成列表进 TLAS；点之类仍记图元不认
    // 7. Device d42c8ee, 4-8 per frame in game of "not a triangle list or under 3 vertices": a draw with fewer than 3 vertices draws nothing and counts as covered; triangle strips (with restarts) are expanded into lists and enter the TLAS; points and the like still count as unsupported primitives
    { uint16_t *st = (uint16_t *)(g_pool + 600000); for (int k = 0; k < 48; k++) st[k] = (uint16_t)((k * 37 + 5) % 512);   // 48 个索引的带 = 46 个三角形 | a 48-index strip = 46 triangles
      uint16_t *rs = (uint16_t *)(g_pool + 600200); uint16_t rsv[8] = {300, 301, 302, 303, 0xFFFF, 304, 305, 306}; memcpy(rs, rsv, sizeof rsv);   // 重启：(300,301,302)、(302,301,303)，再 (304,305,306) | restart: (300,301,302), (302,301,303), then (304,305,306)
      int applied = 0;
      for (int i = 0; i < 12 && !applied; i++) { RTS_HYST_RESET();   // 画面迟滞另测（t_rts_flicker） | picture hysteresis is tested separately (t_rts_flicker)
          frame_open(); prepass(); shadow_pass(); sh(0); sdraw(g_idx, 2, NVN_DRAW_PRIMITIVE_TRIANGLES, 0x2b);   // 2 个顶点的空绘制 | an empty draw of 2 vertices
          sdraw((const uint8_t *)st, 48, NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP, 0x2b); sdraw((const uint8_t *)rs, 8, NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP, 0x2b);
          main_pass(); hdr_again(); frame_close(); applied = map_depth() == 1.0f; }
      EXPECT("空绘制 + 三角形带的帧：光追生效（阴影图清回 1.0）", 1, applied);
      rts_mesh *m46 = NULL, *m3 = NULL; for (uint32_t k = 0; k < g_rts.meshes.cap; k++) { rts_mesh *m = g_rts.meshes.vals[k]; if (m && m->tris == 46) m46 = m; if (m && m->tris == 3 && m->ib && m->ib.length == 36) m3 = m; }
      EXPECT("48 个索引的带建成 46 个三角形", 1, m46 != NULL);
      const uint32_t *t = m3 ? m3->ib.contents : NULL; uint32_t want[9] = {300, 301, 302, 302, 301, 303, 304, 305, 306};
      EXPECT("带重启：展开成 3 个三角形，绕序交替、重启后重新起算", 1, t && !memcmp(t, want, sizeof want));
      long long pr0 = RTS_UNC(RTS_UNC_PRIM); frame_open(); prepass(); shadow_pass(); sh(0); sdraw(g_idx, 48, NVN_DRAW_PRIMITIVE_POINTS, 0x2b); main_pass(); hdr_again(); frame_close();
      EXPECT("点图元：记图元不认 1 次", 1, RTS_UNC(RTS_UNC_PRIM) - pr0); }
#ifdef RTS_UNC
    // 8. 按张处理跨帧保留的阴影图（真机 3fc0f41 / d42c8ee：第 3 / 4 张 256x256 本帧第一次挂上是读入、本帧没清过，进局后约 2000 帧整帧回退）：
    //    同一帧里第 1 张正常清过再画、第 2 张以读入开（内容 0.5 是游戏上一帧留下的）也画 → 第 1 张清回 1.0、第 2 张原样 0.5 不动，原因写明「读入」
    // 8. Per-map handling of a map kept across frames (device 3fc0f41 / d42c8ee: the 3rd / 4th 256x256 map first attached with a load and not cleared this frame, whole frames fell back for about 2000 frames):
    //    in one frame map 1 is cleared and drawn as usual, map 2 opens with a load (content 0.5 left by the game's previous frame) and is drawn too → map 1 back to 1.0, map 2 untouched at 0.5, the reason names the load
    { static tex_t g_keep; g_keep = g_sdepth; g_keep.serial = 79004;
      frame_open(); { RPD *rp = [RPD new]; rp.depthAttachment.texture = tex_mtl(&g_keep); rp.depthAttachment.loadAction = MTLLoadActionClear; rp.depthAttachment.clearDepth = 0.5; rp.depthAttachment.storeAction = MTLStoreActionStore;
        RENC en = [g_cb renderCommandEncoderWithDescriptor:rp]; [en barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice]; m4_end(en); } frame_close();   // 上一帧游戏留下的内容（不经登记） | content the game left last frame (not registered)
      int applied = 0, ok8 = 1; char why[256] = "";
      for (int i = 0; i < 12 && !applied; i++) { RTS_HYST_RESET();   // 画面迟滞另测（t_rts_flicker） | picture hysteresis is tested separately (t_rts_flicker)
          frame_open(); prepass(); shadow_pass(); sh(0);
          memset(&R, 0, sizeof R); R.rt.depth = &g_keep; R.has_rt = 1; rp_begin_ex(0); sh(1);   // 生产开通道：读入 | production pass begin: load
          main_pass(); hdr_again(); ok8 = frame_close() && ok8;
          const char *w = RTS_WHY(); if (w && *w) snprintf(why, sizeof why, "%s", w);
          applied = map_depth() == 1.0f; }
      printf("跨帧保留的那张：原因「%s」\n", why);
      EXPECT("一张跨帧保留：能清的那张照样清回全亮（光追生效）", 1, applied); EXPECT("跨帧保留的那张原样不动（0.5）", 1, tex_depth(&g_keep) == 0.5f);
      EXPECT("清回的只有 1 张", 1, RTS_MAP_CLEARED()); EXPECT("原因写明是读入", 1, strstr(why, "读入") != NULL); EXPECT("这几帧 GPU 无错", 1, ok8); }
#endif
    printf("== %d 项，失败 %d ==\n", count, fail);
    return fail != 0;
}}
#endif
