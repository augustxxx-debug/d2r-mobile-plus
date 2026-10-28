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
#ifndef RTS_LIT_DROP
#define RTS_LIT_DROP() ((void)0)   // 七审之前的代码没有全亮遮罩 | code before the seventh review has no lit mask
#endif
#ifndef RTS_LIT_FAIL_SET
#define RTS_LIT_FAIL_SET(v) ((void)0)   // 八审之前的代码：全亮遮罩分配失败没法注入、也不计尝试次数 | code before the eighth review: lit-mask allocation failure cannot be injected and attempts are not counted
#define RTS_LIT_TRIES() (-1LL)
#define RTS_LIT_RETRY 60
#endif
#ifndef RTS_QUOTAS
#define RTS_QUOTAS_FILL() ((void)0)   // 八审之前：日志名额不随整体释放清零 | before the eighth review: log quotas are not reset on release
#define RTS_QUOTAS() (-1LL)
#endif
#ifndef RTS_WHY
#define RTS_WHY() ""   // 4978d39 及更早：不记跳过追踪的具体原因 | 4978d39 and earlier: the specific reason for skipping the trace is not recorded
#endif
#ifndef RTS_MEMW_STREAK_S
#define mtl_note_memory_warning() ((void)0)   // 内存 P1 之前的代码：系统内存告警光追不管 | code before the memory P1: ray tracing ignores system memory warnings
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
static const tex_t *g_sp = &g_sdepth;   // shadow_pass 开哪张图（缺省主阴影图） | which map shadow_pass opens (the main shadow map by default)
static void shadow_pass(void) {
    rp_end(); id<MTLTexture> t = tex_mtl(g_sp);
    RPD *rp = [RPD new]; rp.depthAttachment.texture = t; rp.depthAttachment.loadAction = MTLLoadActionClear; rp.depthAttachment.clearDepth = 0.25; rp.depthAttachment.storeAction = MTLStoreActionStore;
    if (g_cb) { RENC en = [g_cb renderCommandEncoderWithDescriptor:rp]; [en barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice]; m4_end(en); }
    memset(&R, 0, sizeof R); R.rt.depth = g_sp; R.has_rt = 1;
#ifdef RTS_HAS_NOTE_PASS
    rp.depthAttachment.clearDepth = 1.0; rts_note_pass(rp);   // 游戏的清除值 1.0 = 全亮 | the game's clear value 1.0 = fully lit
#endif
}
static const tex_t *g_dt = &g_sdepth;   // sdraw 画进哪张图（缺省主阴影图） | which map sdraw draws into (the main shadow map by default)
static uint8_t *g_lvp;   // sdraw 用的光源常量（cb3；缺省 g_cb3 = 太阳） | the light constants sdraw uses (cb3; g_cb3 = the sun by default)
static int sdraw(const uint8_t *ib, int n, int prim, uint32_t fmt) {
    memset(&R, 0, sizeof R); R.rt.depth = g_dt; R.has_rt = 1;
    S.nattr = 1; S.attr[0] = (va_st){.fmt = fmt, .offset = 0, .stream = 0}; S.stream[0] = (vs_st){8, 0}; S.vbuf[0] = (NVNbufferRange){(uint64_t)g_vtx, 4096 * 8};
    S.ubo[0][0] = (p_bind_buf){0, 0, (uint64_t)g_mat, 64}; S.ubo[0][3] = (p_bind_buf){0, 3, (uint64_t)(g_lvp ? g_lvp : g_cb3), 400};
    p_draw_idx di = {prim, 1, n, 0, 0, 1, (uint64_t)ib};
    return rts_draw(g_pr, NULL, &di, prim);
}
static int qdraw(int first, int count) {   // 非索引四边形（prim 7）阴影绘制 | a non-indexed quad (prim 7) shadow draw
    memset(&R, 0, sizeof R); R.rt.depth = g_dt; R.has_rt = 1;
    S.nattr = 1; S.attr[0] = (va_st){.fmt = 0x2b, .offset = 0, .stream = 0}; S.stream[0] = (vs_st){8, 0}; S.vbuf[0] = (NVNbufferRange){(uint64_t)g_vtx, 4096 * 8};
    S.ubo[0][0] = (p_bind_buf){0, 0, (uint64_t)g_mat, 64}; S.ubo[0][3] = (p_bind_buf){0, 3, (uint64_t)(g_lvp ? g_lvp : g_cb3), 400};
    p_draw dw = {.prim = NVN_DRAW_PRIMITIVE_QUADS, .first = first, .count = count}; return rts_draw(g_pr, &dw, NULL, NVN_DRAW_PRIMITIVE_QUADS);
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
// 七审 Q7-01：按生产 _rt 变体同样的表达式读遮罩（rts_variant.py：fast::min(rts_mask.read(uint2(gl_FragCoord.xy)).x, 原版采样)，原版采样取全亮 1.0），
// 扫整个渲染目标（w×h，每个像素一条线程，线程坐标 = gl_FragCoord.xy 的整数部分），数被压暗（< 1）的像素
// Seventh review Q7-01: read the mask with the same expression as the production _rt variants (rts_variant.py: fast::min(rts_mask.read(uint2(gl_FragCoord.xy)).x, original sample), the original sample being fully lit 1.0),
// over the whole render target (w×h, one thread per pixel, thread position = the integer part of gl_FragCoord.xy), counting darkened (< 1) pixels
static long dark_pixels(id<MTLTexture> mask, NSUInteger w, NSUInteger h) {
    static id<MTLComputePipelineState> ps; static id<MTL4ArgumentTable> at; static id<MTLBuffer> buf;
    if (!ps) { NSError *e = nil;
        id<MTLLibrary> lib = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\nkernel void neutral(texture2d<float> rts_mask [[texture(0)]], device uint *o [[buffer(0)]], uint2 g [[thread_position_in_grid]]) {\n"
            "  if (g.x >= o[1] || g.y >= o[2]) return; float v = fast::min(rts_mask.read(uint2(float2(g) + 0.5)).x, 1.0f); if (v < 0.999f) atomic_fetch_add_explicit((device atomic_uint *)&o[0], 1u, memory_order_relaxed); }\n" options:nil error:&e];
        if (!lib) { printf("dark_pixels 核编不过：%s\n", e.description.UTF8String); return -1; }
        ps = [g_dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"neutral"] error:&e];
        MTL4ArgumentTableDescriptor *ad = [MTL4ArgumentTableDescriptor new]; ad.maxBufferBindCount = 1; ad.maxTextureBindCount = 1; at = [g_dev newArgumentTableWithDescriptor:ad error:&e];
        buf = [g_dev newBufferWithLength:16 options:MTLResourceStorageModeShared]; res_add(buf); }
    if (!mask || !ps) return -1;
    uint32_t *o = buf.contents; o[0] = 0; o[1] = (uint32_t)w; o[2] = (uint32_t)h;
    mtl_sub_begin(); rp_end(); CENC ce = (CENC)enc_c_begin(NULL); [ce setArgumentTable:at]; enc_c_pso(ce, ps); [at setTexture:mask.gpuResourceID atIndex:0]; [at setAddress:buf.gpuAddress atIndex:0];
    enc_c_dispatch(ce, MTLSizeMake((w + 7) / 8, (h + 7) / 8, 1), MTLSizeMake(8, 8, 1)); enc_c_end(ce); args_dirty_all();
    uint64_t f = mtl_sub_commit(); mtl_wait(f, 5000000000ull); return (long)o[0];
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
    setvbuf(stdout, NULL, _IONBF, 0); unsetenv("DM_RT_SHADOW"); setenv("DM_RT_SHADOW_CACHE_MB", "1", 1); setenv("DM_RT_SHADOW_DBG", "1", 1); setenv("DM_RT_SHADOW_PART", "0", 1); mtl_init();   // 前面各节测「整帧清图」对照模式（DM_RT_SHADOW_PART=0）；生产缺省的局部回退另有几节（g_rts.part = 1） | the earlier sections test the clear-the-frame comparison mode (DM_RT_SHADOW_PART=0); the production default, partial fallback, has sections of its own (g_rts.part = 1)   // rt2：测试里没有 _rt 变体，调试模式下用叠加合成（审计分支「无变体 = 叠加」的同一路径） | rt2: no _rt variants in the test; debug mode composites with the overlay (the audit branch's "no variants = overlay" path)
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
    { int b2[2] = {0, 1}; enc = run_frame(1, b2, 2, &ok);   // 网格 1 第一次出现：本帧排队、在本帧光追编码器里先建再进 TLAS（同一编码器内有序，不再多等一帧） | mesh 1 appears for the first time: queued this frame, built first in this frame's ray-tracing encoder and then in the TLAS (ordered within one encoder, no extra frame)
      EXPECT("有新网格的当帧：两次阴影绘制照常光栅化（兜底）", 2, enc); EXPECT("有新网格的当帧：新网格当帧进 TLAS，阴影图清回全亮", 1, map_depth() == 1.0f); EXPECT("有新网格的当帧：合成了遮罩", 1, g_rts.overlay_done);
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
      RTS_QUOTAS_FILL();   // 八审观察 2：日志名额用完、全亮遮罩在等重试 | eighth review observation 2: log quotas used up, the lit mask waiting for a retry
      g_rts.on = 0;
      for (int i = 0; i < 4; i++) @autoreleasepool { frame_open(); sh(0); frame_close(); }
      int g_as = 0, g_tl = 0, g_mk = 0;
      for (int i = 0; i < 100 && !(g_as && g_tl && g_mk); i++) {   // GPU 做完之后 Metal 内部的引用是异步放掉的：最多等 2 秒（每 20 ms 空跑一帧让延迟释放表走一遍） | Metal drops its internal references asynchronously after the GPU finishes: wait up to 2 s (an empty frame every 20 ms runs the deferred-release table)
          @autoreleasepool { frame_open(); frame_close(); g_as = w_as == nil; g_tl = w_tlas == nil; g_mk = w_mask == nil; } if (!(g_as && g_tl && g_mk)) usleep(20000); }
      EXPECT("关掉后「盖不住」日志名额、全亮遮罩重试等待清零（八审观察 2）", 0, RTS_QUOTAS());
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
    //    一帧 RTS_BUILDS_PER_FRAME + 2 个新网格 → 前 RTS_BUILDS_PER_FRAME 个当帧建好进 TLAS（不记待建）、2 个配额满；下一帧阴影绘制所在的通道没登记 → 通道没登记
    // 6. Draws outside the TLAS in ray-traced frames are counted by reason (device run of 3fc0f41: about 8 per frame after walking somewhere, and the log could not tell why):
    //    RTS_BUILDS_PER_FRAME + 2 new meshes in one frame → the first RTS_BUILDS_PER_FRAME are built and enter the TLAS that frame (no "pending"), 2 over the quota; next frame a shadow draw in an unregistered pass → pass not registered
    { EXPECT("分原因前光追生效", 1, until_applied(a, 1));
      long long q0 = RTS_UNC(RTS_UNC_QUOTA), p0 = RTS_UNC(RTS_UNC_PENDING), s0 = RTS_UNC(RTS_UNC_PASS);
      frame_open(); prepass(); shadow_pass(); for (int i = 10; i < 10 + RTS_BUILDS_PER_FRAME + 2; i++) sh(i); main_pass(); hdr_again(); frame_close();
      EXPECT("新建配额满：记 2 次", 2, RTS_UNC(RTS_UNC_QUOTA) - q0); EXPECT("新网格当帧进 TLAS：不记待建", 0, RTS_UNC(RTS_UNC_PENDING) - p0);
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
          memset(&R, 0, sizeof R); R.rt.depth = &g_keep; R.has_rt = 1; rp_begin_ex(0); g_dt = &g_keep; sh(1); g_dt = &g_sdepth;   // 生产开通道：读入；这次画进跨帧保留的那张 | production pass begin: load; this draw goes into the kept map
          main_pass(); hdr_again(); ok8 = frame_close() && ok8;
          const char *w = RTS_WHY(); if (w && *w) snprintf(why, sizeof why, "%s", w);
          applied = map_depth() == 1.0f; }
      printf("跨帧保留的那张：原因「%s」\n", why);
      EXPECT("一张跨帧保留：能清的那张照样清回全亮（光追生效）", 1, applied); EXPECT("跨帧保留的那张原样不动（0.5）", 1, tex_depth(&g_keep) == 0.5f);
      EXPECT("清回的只有 1 张", 1, RTS_MAP_CLEARED()); EXPECT("原因写明是读入", 1, strstr(why, "读入") != NULL); EXPECT("这几帧 GPU 无错", 1, ok8); }
    // 9. 帧号在已登记的阴影通道开着时前进（Mac 实测：每帧第一个阴影通道在上一帧号下开、登记，第一次绘制时才进新帧）：这张图要带进新帧，
    //    不能算「通道没登记」（合并 d42c8ee 之后 Mac 营地 3600 帧里 3487 帧因此整帧清不了图）
    // 9. The frame number advances while a registered shadow pass is open (measured on the Mac: the first shadow pass of each frame opens and registers under the previous frame number, the new frame starts at its first draw):
    //    the map must be carried into the new frame, not counted as "pass not registered" (after merging d42c8ee, 3487 of 3600 camp frames on the Mac could not clear their maps because of this)
    { long long s0 = RTS_UNC(RTS_UNC_PASS); int cleared = 0, ok9 = 1;
      frame_open(); prepass(); shadow_pass(); sh(0); main_pass(); hdr_again(); shadow_pass(); ok9 = frame_close() && ok9;   // 帧尾开下一帧的阴影通道 | the next frame's shadow pass opens at the end of this frame
      for (int i = 0; i < 12 && !cleared; i++) { RTS_HYST_RESET();
          frame_open(); sh(0); prepass(); main_pass(); hdr_again(); cleared = RTS_MAP_CLEARED() > 0; shadow_pass(); ok9 = frame_close() && ok9; }
      EXPECT("帧号在阴影通道开着时前进：这张图照样清回全亮", 1, cleared); EXPECT("不记「通道没登记」", 0, RTS_UNC(RTS_UNC_PASS) - s0); EXPECT("这几帧 GPU 无错", 1, ok9); }
    // 10. 绘制赶在通道登记之前（Mac 实测：rts_draw 在编码前调，每帧第一个阴影通道的第一次绘制时通道还没开、没登记）：按渲染目标找到这张图，
    //     通道开时补上「清过」，不能算「通道没登记」
    // 10. A draw before its pass is registered (measured on the Mac: rts_draw runs before encoding, so the first draw of each frame's first shadow pass comes before the pass is opened and registered): the map is found by the render target,
    //     the pass fills in "cleared" when it opens, and the draw must not count as "pass not registered"
    { RTS_HYST_RESET(); EXPECT("第 10 节前光追生效（帧尾的通道已画过，不会带进下一帧）", 1, until_applied(a, 1));   // 隔开第 9 节帧尾开的通道 | isolates the pass section 9 opened at a frame end
      long long s0 = RTS_UNC(RTS_UNC_PASS); int cleared = 0, ok10 = 1;
      for (int i = 0; i < 12 && !cleared; i++) { RTS_HYST_RESET();
          frame_open(); prepass(); rp_end(); memset(&R, 0, sizeof R); R.rt.depth = &g_sdepth; R.has_rt = 1; sh(0);   // 先画（通道还没开） | draw first (pass not open yet)
          shadow_pass(); sh(1); main_pass(); hdr_again(); ok10 = frame_close() && ok10; cleared = map_depth() == 1.0f; }
      EXPECT("绘制先于通道登记：这张图照样清回全亮", 1, cleared); EXPECT("不记「通道没登记」（绘制先于登记）", 0, RTS_UNC(RTS_UNC_PASS) - s0); EXPECT("这几帧 GPU 无错", 1, ok10); }
    // 11. 光源不是太阳的阴影图（营地实测：512² 二维图光源竖直向下、角色脚下的俯视软影；256² 头发图）：光追的太阳遮罩不替它，本帧清过也照原样留着，
    //     不然 NPC 脚下的影子就没了（真机 79eaa3f 用户录屏）；太阳那张照常清回全亮
    // 11. A shadow map whose light is not the sun (measured in camp: a 512² 2D map lit straight down, the soft top-down shadow under characters; the 256² hair map): the ray-traced sun mask does not replace it, so it stays as drawn even though cleared this frame,
    //     otherwise the shadows under NPCs disappear (user recording on the device with 79eaa3f); the sun map is cleared back as usual
    { static tex_t g_top; static uint8_t *down; g_top = g_sdepth; g_top.serial = 79005;
      if (!down) { down = g_pool + 12288; float l[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; memcpy(down + 336, l, 64); }   // 第 0、1 列叉积 = z：与太阳 (0, 1, 0) 垂直 | cross of columns 0 and 1 = z: perpendicular to the sun (0, 1, 0)
      RTS_HYST_RESET(); EXPECT("第 11 节前光追生效", 1, until_applied(a, 1));
      int cleared = 0, ok11 = 1; char why[256] = "";
      for (int i = 0; i < 12 && !cleared; i++) { RTS_HYST_RESET();
          frame_open(); prepass(); shadow_pass(); sh(0);
          g_sp = &g_top; shadow_pass(); g_sp = &g_sdepth; g_dt = &g_top; g_lvp = down; sh(1); g_dt = &g_sdepth; g_lvp = NULL;   // 第二张：本帧清过、画过，光源竖直 | the second map: cleared and drawn this frame, light straight down
          main_pass(); hdr_again(); ok11 = frame_close() && ok11; const char *w = RTS_WHY(); if (w && *w) snprintf(why, sizeof why, "%s", w); cleared = map_depth() == 1.0f; }
      printf("非太阳那张：原因「%s」\n", why);
      EXPECT("有非太阳的图：太阳那张照常清回全亮", 1, cleared); EXPECT("非太阳那张原样不动（0.25）", 1, tex_depth(&g_top) == 0.25f); EXPECT("清回的只有太阳那 1 张", 1, RTS_MAP_CLEARED());
      EXPECT("原因写明光源不是太阳", 1, strstr(why, "光源不是太阳") != NULL); EXPECT("这几帧 GPU 无错", 1, ok11); }
    // 12. GPU 挂起保护的暂停（不在自动降档序列里，七审三轮）：画面原版、不算网格键、不算失败；暂停结束照常光追
    // 12. The GPU-hang protection pause (not part of the automatic downgrade, seventh review round 3): original picture, no mesh key, not a failure; ray tracing resumes when it ends
    { RTS_HYST_RESET(); EXPECT("第 12 节前光追生效", 1, until_applied(a, 1));
      int lv0 = g_rts.gov.lvl; RTS_PAUSE_SET(600); int ok12 = 1, applied = 0; uint64_t k0 = g_rts.key_ns; int bo0 = g_rts.backoff;
      for (int i = 0; i < 4; i++) { ok12 = run_frame(1, a, 1, &ok12) >= 0 && ok12; applied += map_depth() == 1.0f; }
      EXPECT("暂停：阴影图一帧都没清（原版画面）", 0, applied); EXPECT("暂停：不算网格键", 0, (long long)(g_rts.key_ns - k0)); EXPECT("暂停：不算失败（不退避）", bo0, g_rts.backoff);
      EXPECT("最低自动档（第 3 档）不是暂停", 0, rts_lvl_of(RTS_LVL_MAX).off); RTS_PAUSE_SET(0); g_rts.gov.lvl = lv0; RTS_HYST_RESET(); EXPECT("回第 0 档后光追照常", 1, until_applied(a, 1)); }
#endif
#ifdef RTS_PART_SET
    // 13–18. 局部回退（生产缺省；七审三轮 P1 / P2 / 第 1、6 条）：真机 79eaa3f 营地 / 森林只有 26% / 31% 的时间是光追画面——一帧里只要一次阴影绘制没进 TLAS（新建配额满 / 网格待建），整帧回原版，
    //        画面在光追与原版之间来回跳。现在进了 TLAS 的太阳阴影绘制不画进阴影图，没进的照原版画进去，_rt 变体取两者较暗值；阴影图不清。太阳按最大那张图认（第 6 条：头发 / 俯视图先画时太阳方向错了）
    // 13–18. Partial fallback (production default; seventh review round 3 P1 / P2 / items 1, 6): on the device with 79eaa3f the camp / forest showed ray tracing only 26% / 31% of the time - one shadow draw outside the TLAS (build quota full / mesh pending) sent the whole frame back to original,
    //        and the picture jumped between ray traced and original. Now sun shadow draws in the TLAS are not drawn into the map, the others are drawn as original, and the _rt variants take the darker of the two; maps are not cleared. The sun is taken from the largest map (item 6: with the hair / top-down map drawn first the sun pointed the wrong way)
    { RTS_PART_SET(1); static uint8_t *tilt, *other; static tex_t g_small;
      if (!tilt) { tilt = g_pool + 20480; float l[16] = {1, 0, 0, 0, 0, 0.6f, 0.8f, 0, 0, 0.8f, -0.6f, 0, 0, 0, 0, 1}; memcpy(tilt + 336, l, 64);   // 太阳 (0, 0.8, −0.6)：斜照（竖直的在局部回退里不算太阳） | sun (0, 0.8, −0.6): slanted (a vertical light is not the sun under partial fallback)
                   other = g_pool + 24576; float o[16] = {1, 0, 0, 0, 0, 0.8f, 0.6f, 0, 0, 0.6f, -0.8f, 0, 0, 0, 0, 1}; memcpy(other + 336, o, 64); }   // 另一盏 (0, 0.6, −0.8) | another light (0, 0.6, −0.8)
      g_small = g_sdepth; g_small.serial = 79006; g_small.d.width = g_small.d.height = 128;
      g_lvp = tilt; int b2[2] = {0, 1}, ok13 = 1;
      #define UNTIL_PART(ids, n) ({ int u_ = 0; for (int i_ = 0; i_ < 12 && !u_; i_++) { RTS_HYST_RESET(); run_frame(1, ids, n, &ok); ok13 = ok13 && ok; u_ = RTS_MAP_CLEARED() > 0; } u_; })
      // 13. 稳定帧：进了 TLAS 的不编码，遮罩接替，阴影图不清 | 13. steady frame: draws in the TLAS are not encoded, the mask takes over, the map is not cleared
      EXPECT("局部回退：能进入光追画面", 1, UNTIL_PART(b2, 2));
      enc = run_frame(1, b2, 2, &ok); ok13 = ok13 && ok;
      EXPECT("局部回退：进了 TLAS 的两次绘制都不编码", 0, enc); EXPECT("局部回退：本帧用上遮罩", 1, RTS_MAP_CLEARED()); EXPECT("局部回退：变体拿到的是光追遮罩", 1, rts_mask_tex() == g_rts.mask);
      EXPECT("局部回退：阴影图不清（0.25 原样）", 1, map_depth() == 0.25f); EXPECT("局部回退：太阳是斜照那盏 (0, 0.8, −0.6)", 1, fabsf(g_rts.sun[1] - 0.8f) < 1e-3f && fabsf(g_rts.sun[2] + 0.6f) < 1e-3f);
      // 14. 有绘制没进 TLAS（点图元 / 新建配额满）：只有它照原版画，本帧照样是光追画面、画面不切换 | 14. draws outside the TLAS (points / build quota full): only they are drawn as original, the frame still shows ray tracing and the picture does not switch
      { long long sw0 = RTS_SWITCHES(); uint64_t rf0 = g_rts.n_resid_frames;
        frame_open(); prepass(); shadow_pass(); int e0 = sh(0), e1 = sdraw(g_idx, 48, NVN_DRAW_PRIMITIVE_POINTS, 0x2b); main_pass(); hdr_again(); ok13 = frame_close() && ok13;
        EXPECT("有点图元的帧：进了 TLAS 的照样不编码", 1, e0); EXPECT("有点图元的帧：点图元照原版画（返回 0）", 0, e1); EXPECT("有点图元的帧：照样是光追画面", 1, RTS_MAP_CLEARED());
        uint16_t *fresh = (uint16_t *)(g_pool + 700000); for (int k = 0; k < (RTS_BUILDS_PER_FRAME + 2) * 48; k++) fresh[k] = (uint16_t)(((k * 2654435761u) >> 9) % 4096);   // 一帧冒出的新网格 | new meshes appearing in one frame
        int skipped = 0, drawn = 0; frame_open(); prepass(); shadow_pass(); sh(0);
        for (int i = 0; i < RTS_BUILDS_PER_FRAME + 2; i++) { int r = sdraw((const uint8_t *)(fresh + i * 48), 48, NVN_DRAW_PRIMITIVE_TRIANGLES, 0x2b); skipped += r == 1; drawn += r == 0; }
        main_pass(); hdr_again(); ok13 = frame_close() && ok13;
        EXPECT("新建配额满的帧：配额内的当帧建好、不编码", RTS_BUILDS_PER_FRAME, skipped); EXPECT("新建配额满的帧：超出的 2 个照原版画", 2, drawn); EXPECT("新建配额满的帧：照样是光追画面", 1, RTS_MAP_CLEARED());
        run_frame(1, b2, 2, &ok); ok13 = ok13 && ok;
        EXPECT("之后一帧仍是光追画面", 1, RTS_MAP_CLEARED()); EXPECT("这几帧画面一次都没切换", sw0, RTS_SWITCHES()); EXPECT("局部回退帧记了 2 帧", 2, (long long)(g_rts.n_resid_frames - rf0)); }
      // 15. 太阳按图认：小图（128²、另一盏光）先画也不当太阳；它照原版画、不进 TLAS | 15. sun by map: a smaller map (128², another light) drawn first is not the sun; it is drawn as original and kept out of the TLAS
      { uint64_t ns0 = g_rts.n_nonsun_draws;
        frame_open(); prepass(); g_sp = &g_small; shadow_pass(); g_sp = &g_sdepth; g_dt = &g_small; g_lvp = other; int es = sh(2); g_dt = &g_sdepth; g_lvp = tilt;
        shadow_pass(); int ex = sh(0); int nin = g_rts.ninst; main_pass(); hdr_again(); ok13 = frame_close() && ok13;
        EXPECT("小图先画：照原版画（返回 0）", 0, es); EXPECT("小图先画：没进 TLAS（只有大图那 1 个实例）", 1, nin); EXPECT("小图先画：大图照样不编码", 1, ex);
        EXPECT("太阳取自 256² 那张", 256, g_rts.sun_from_w); EXPECT("太阳方向 = 大图的光 (0, 0.8, −0.6)", 1, fabsf(g_rts.sun[1] - 0.8f) < 1e-3f); EXPECT("非太阳图记了 1 次", 1, (long long)(g_rts.n_nonsun_draws - ns0)); EXPECT("小图那帧照样是光追画面", 1, RTS_MAP_CLEARED()); }
      // 16. 只有竖直光的图（俯视软影）：不是太阳、照原版画、不进 TLAS，也不算失败 | 16. a map lit only straight down (top-down soft shadow): not the sun, drawn as original, kept out of the TLAS, and not a failure
      { int bo0 = g_rts.backoff; g_lvp = NULL; frame_open(); prepass(); shadow_pass(); int ev = sh(0); int nin = g_rts.ninst; main_pass(); hdr_again(); ok13 = frame_close() && ok13; g_lvp = tilt;
        const char *w = RTS_WHY(); EXPECT("竖直光：照原版画（返回 0）", 0, ev); EXPECT("竖直光：没进 TLAS", 0, nin); EXPECT("竖直光：不算「没取到太阳方向」", 1, !w || !strstr(w, "太阳"));
        run_frame(1, b2, 2, &ok); ok13 = ok13 && ok; EXPECT("竖直光那帧不算失败（不退避）", bo0, g_rts.backoff); }
      // 17. 省画之后才发现追不成（缺相机）：这一帧那些遮挡体没影子（记【红】），下一帧照原版全画，之后恢复 | 17. the trace turns out impossible after the draws were skipped (no camera): those occluders have no shadow this frame (logged in red), the next frame draws everything as original, then it recovers
      { RTS_HYST_RESET(); EXPECT("第 17 节前是光追画面", 1, UNTIL_PART(b2, 2)); uint64_t l0 = g_rts.n_skip_lost;
        enc = run_frame(0, b2, 2, &ok); ok13 = ok13 && ok; EXPECT("缺相机的当帧：帧开头不知道，照样不编码", 0, enc); EXPECT("缺相机的当帧：遮罩没用上", 0, RTS_MAP_CLEARED());
        // 17b. 不用太阳遮罩的帧绑的那张必须处处全亮：_rt 变体按像素坐标读遮罩（rts_mask.read(gl_FragCoord)），原先绑 1x1 白贴图，越界读是 0 = 整屏太阳全影、画面压暗（6534ef4 起） | 17b. the texture bound on a frame that does not use the sun mask must be lit everywhere: the _rt variants read the mask by pixel coordinates, and the 1x1 white texture bound before read 0 out of bounds = the whole screen in sun shadow, darkening the picture (since 6534ef4)
        { id<MTLTexture> mt = rts_mask_tex(); uint8_t px = 0; if (mt && mt.width > 600 && mt.height > 300 && mt.storageMode == MTLStorageModeShared) [mt getBytes:&px bytesPerRow:mt.width fromRegion:MTLRegionMake2D(600, 300, 1, 1) mipmapLevel:0];
          EXPECT("不用遮罩的帧：绑的遮罩是主深度尺寸（640x360）", 1, mt && mt.width == 640 && mt.height == 360); EXPECT("不用遮罩的帧：(600, 300) 处也是 1.0", 255, px); }
        enc = run_frame(1, b2, 2, &ok); ok13 = ok13 && ok; EXPECT("下一帧：记「省画后没追成」1 帧", 1, (long long)(g_rts.n_skip_lost - l0)); EXPECT("下一帧（退避）：两次都照原版画", 2, enc);
        EXPECT("之后恢复光追画面", 1, UNTIL_PART(b2, 2)); }
      // 18. 以读入开的太阳图（本帧没清，留着上一帧的内容）：进了 TLAS 的也照画，不留旧影 | 18. a sun map opened with a load (not cleared this frame, last frame's content kept): draws in the TLAS are drawn anyway, leaving no stale shadow
      { uint64_t ld0 = g_rts.n_skip_load; RTS_HYST_RESET(); UNTIL_PART(b2, 2);
        frame_open(); prepass(); rp_end(); memset(&R, 0, sizeof R); R.rt.depth = &g_sdepth; R.has_rt = 1; rp_begin_ex(0); int el = sh(0); main_pass(); hdr_again(); ok13 = frame_close() && ok13;
        EXPECT("读入开的太阳图：进了 TLAS 的照画（返回 0）", 0, el); EXPECT("读入开的太阳图：记 1 次", 1, (long long)(g_rts.n_skip_load - ld0)); }
      // 18b. 没有主通道的帧（读图 / 切关时只画了阴影图）：屏幕上没东西，不退避、不算画面切换、不推高迟滞要求 | 18b. a frame without a main pass (only shadow maps drawn while loading / changing levels): nothing on screen, no backoff, no picture switch, no higher hysteresis requirement
      { RTS_HYST_RESET(); UNTIL_PART(b2, 2); long long sw0 = RTS_SWITCHES(); int bo0 = g_rts.backoff, need0 = RTS_NEED();
        frame_open(); prepass(); shadow_pass(); sh(0); ok13 = frame_close() && ok13;
        run_frame(1, b2, 2, &ok); ok13 = ok13 && ok;
        EXPECT("没有主通道的帧：不退避", bo0, g_rts.backoff); EXPECT("没有主通道的帧：不算画面切换", sw0, RTS_SWITCHES()); EXPECT("没有主通道的帧：迟滞要求不变", need0, RTS_NEED()); EXPECT("之后一帧照样是光追画面", 1, RTS_MAP_CLEARED()); }
      // 19. GPU 挂起保护的暂停：局部回退也不省画 | 19. the hang-protection pause: partial fallback skips nothing either
      { RTS_PAUSE_SET(600); int drawn = 0; for (int i = 0; i < 3; i++) { drawn += run_frame(1, b2, 2, &ok); ok13 = ok13 && ok; }
        EXPECT("暂停：三帧六次都照原版画", 6, drawn); EXPECT("暂停：没用遮罩", 0, RTS_MAP_CLEARED()); RTS_PAUSE_SET(0); }
      // 20. 第 3 档（最低自动档）：镂空遮挡体照原版画进阴影图、不进 TLAS，其余照常光追（七审三轮第 5、8 条：森林的镂空判定最重） | 20. level 3 (the lowest automatic level): alpha-tested occluders go into the map as original and stay out of the TLAS, everything else is ray traced as usual (seventh review round 3 items 5, 8: the alpha judge is the heaviest part in the forest)
      { RTS_HYST_RESET(); UNTIL_PART(b2, 2); int lv0 = g_rts.gov.lvl; g_rts.gov.lvl = 3; NvProg *ap = [NvProg new]; ap->sh[0] = g_pr->sh[0]; ap->sh[1] = [NvShader new]; ap->sh[1]->rf.may_discard = 1;
        uint64_t lw0 = g_rts.na_why[RA_LEVEL]; frame_open(); prepass(); shadow_pass(); int eo = sh(0); NvProg *keep = g_pr; g_pr = ap; int ea = sh(5); g_pr = keep; int nin = g_rts.ninst; main_pass(); hdr_again(); ok13 = frame_close() && ok13;
        EXPECT("第 3 档：镂空照原版画（返回 0）", 0, ea); EXPECT("第 3 档：镂空没进 TLAS", 1, nin); EXPECT("第 3 档：镂空记「降档照原版」1 次", 1, (long long)(g_rts.na_why[RA_LEVEL] - lw0));
        EXPECT("第 3 档：不透明的照样不编码", 1, eo); EXPECT("第 3 档：照样是光追画面", 1, RTS_MAP_CLEARED()); g_rts.gov.lvl = lv0; }
      // 21. 开关关着时完全无副作用（CLAUDE.md 闸 3 · B 级）：阴影绘制全照原版编码，局部回退 / 太阳认图的计数一个不动，不写深度标志不置位 | 21. no side effect at all with the switch off (CLAUDE.md gate 3 · level B): every shadow draw is encoded as original, the partial-fallback / sun-map counters do not move, the no-depth-write flag is never set
      { uint64_t s0 = g_rts.n_skip_draws, r0 = g_rts.n_resid_draws, n0 = g_rts.n_nonsun_draws, k0 = g_rts.key_ns; g_rts.on = 0; int drawn = 0;
        for (int i = 0; i < 3; i++) { drawn += run_frame(1, b2, 2, &ok); ok13 = ok13 && ok; }
        EXPECT("开关关：三帧六次都照原版编码", 6, drawn); EXPECT("开关关：省画 / 照原版画 / 非太阳图计数都不动", 0, (long long)(g_rts.n_skip_draws - s0 + g_rts.n_resid_draws - r0 + g_rts.n_nonsun_draws - n0));
        EXPECT("开关关：不算网格键", 0, (long long)(g_rts.key_ns - k0)); EXPECT("开关关：不写深度标志没置位", 0, g_rts_nowrite); EXPECT("开关关：遮罩不绑", 1, rts_mask_tex() == nil || rts_mask_tex() == g_rts.white); g_rts.on = 1; }
      // 23. 七审 Q7-01：选上 _rt 变体时它读到的遮罩在整个渲染目标上都得是对的——迟滞等待、只有点光、审计的直接状态（还没建全亮遮罩）、动态分辨率、恢复；光追关时不选变体。
      //     像素按生产变体同样的读法（dark_pixels）；原先 1x1 白贴图越界读是 0，整屏压暗 | 23. seventh review Q7-01: when an _rt variant is selected, the mask it reads must be right over the whole render target - hysteresis hold, point lights only, the review's direct state (no lit mask built yet), dynamic resolution, recovery; with ray tracing off no variant is selected.
      //     Pixels are read the way the production variants read them (dark_pixels); the 1x1 white texture used to read 0 out of bounds and darken the whole screen
      { NvProg *vp = [NvProg new]; vp->sh[0] = g_pr->sh[0]; vp->sh[1] = [NvShader new]; vp->fs_base = vp->sh[1]; vp->fs_rt = [NvShader new]; vp->rt_tried = 1;
        static tex_t big; big = g_mdepth; big.serial = 79101; big.d.width = 800; big.d.height = 450; static tex_t bighdr; bighdr = g_hdr; bighdr.serial = 79102; bighdr.d.width = 800; bighdr.d.height = 450;
        #define SELECT(c, d) ({ R.rt.n = 1; R.rt.color[0] = (c); R.rt.depth = (d); g_rts.hdr_ser = (c)->serial; g_rts.ds_ser = (d)->serial; g_rts.trace_pending = 0; S.ds.dt = 1; S.color.blend_en = 0; vp->sh[1] = vp->fs_base; int dbg_ = g_rts.dbg; g_rts.dbg = 0; rts_select_fs(vp); g_rts.dbg = dbg_; vp->sh[1] == vp->fs_rt; })   // 调试叠加模式不选变体：选的这一下关掉 | the debug-overlay mode never selects variants: off for this selection
        // a. 迟滞等待帧：缺相机失败一帧之后，遮罩照算、画面原版 | a. hysteresis hold: after one failed frame without a camera the mask is traced, the picture original
        RTS_HYST_RESET(); UNTIL_PART(b2, 2); run_frame(0, b2, 2, &ok); int hold = 0;
        for (int i = 0; i < 10 && !hold; i++) { run_frame(1, b2, 2, &ok); ok13 = ok13 && ok; hold = g_rts.mask_done && !RTS_MAP_CLEARED(); }   // 退避几帧后进入迟滞等待 | hysteresis hold follows a few backoff frames
        int sel = SELECT(&g_hdr, &g_mdepth);
        EXPECT("Q7-01 迟滞等待帧：遮罩算了、画面原版（测试前提）", 1, hold); EXPECT("Q7-01 迟滞等待帧：选上 _rt 变体", 1, sel);
        EXPECT("Q7-01 迟滞等待帧：整个 640x360 目标没有一个像素被压暗", 0, dark_pixels(rts_mask_tex(), 640, 360));
        // b. 只有点光（太阳遮罩没算） | b. point lights only (no sun mask)
        g_rts.mask_done = 0; g_rts.pl_done = 1; sel = SELECT(&g_hdr, &g_mdepth); g_rts.pl_done = 0;
        EXPECT("Q7-01 只有点光：选上 _rt 变体", 1, sel); EXPECT("Q7-01 只有点光：没有像素被压暗", 0, dark_pixels(rts_mask_tex(), 640, 360));
        // c. 审计的直接状态：遮罩算了、没清图、全亮遮罩还没建（第一帧 / 刚整体释放） | c. the review's direct state: mask traced, maps not cleared, no lit mask yet (first frame / just released)
        g_rts.mask_done = 1; g_rts.map_cleared = 0; RTS_LIT_DROP(); sel = SELECT(&g_hdr, &g_mdepth);
        EXPECT("Q7-01 全亮遮罩还没建：选上变体时没有像素被压暗（当场按目标尺寸建）", 0, sel ? dark_pixels(rts_mask_tex(), 640, 360) : 0); EXPECT("Q7-01 全亮遮罩还没建：绑的不是 1x1", 1, !sel || rts_mask_tex().width == 640);
        // d. 动态分辨率：目标变成 800x450 | d. dynamic resolution: the target becomes 800x450
        sel = SELECT(&bighdr, &big);
        EXPECT("Q7-01 动态分辨率（遮罩不用）：800x450 没有像素被压暗", 0, sel ? dark_pixels(rts_mask_tex(), 800, 450) : 0); EXPECT("Q7-01 动态分辨率（遮罩不用）：绑的盖住 800x450", 1, !sel || (rts_mask_tex().width >= 800 && rts_mask_tex().height >= 450));
        RTS_HYST_RESET(); UNTIL_PART(b2, 2); id<MTLTexture> traced = rts_mask_tex();
        sel = SELECT(&bighdr, &big); EXPECT("Q7-01 动态分辨率（光追遮罩 640x360、目标 800x450）：不选变体、照原版着色", 0, sel);
        // e. 恢复：光追画面，变体读本帧光追遮罩 | e. recovery: the ray-traced picture, the variant reads this frame's traced mask
        sel = SELECT(&g_hdr, &g_mdepth); EXPECT("Q7-01 恢复：选上变体、读本帧光追遮罩", 1, sel && rts_mask_tex() == g_rts.mask && traced == g_rts.mask);
        // f. 光追关：不选变体 | f. ray tracing off: no variant
        g_rts.on = 0; sel = SELECT(&g_hdr, &g_mdepth); g_rts.on = 1; EXPECT("Q7-01 光追关：不选变体", 0, sel);
        #undef SELECT
        memset(&R, 0, sizeof R); }
      // 24. 七审 Q7-02：非索引四边形（prim 7）按原版光栅化的拓扑建网格（每 4 个顶点 0,1,2,0,2,3、尾巴不画）；不足 4 个顶点是空绘制；带索引的四边形原版不画，光追也不建
      // 24. seventh review Q7-02: non-indexed quads (prim 7) build their mesh with the original rasterizer's topology (0,1,2,0,2,3 per 4 vertices, the tail not drawn); fewer than 4 vertices is an empty draw; indexed quads are not drawn by the original, so ray tracing builds nothing
      { RTS_HYST_RESET(); UNTIL_PART(b2, 2); uint64_t e0 = g_rts.n_empty; int nm0 = g_rts.nmesh;
        frame_open(); prepass(); shadow_pass(); qdraw(0, 4); qdraw(8, 10); qdraw(40, 3);
        p_draw_idx qi = {NVN_DRAW_PRIMITIVE_QUADS, 1, 8, 0, 0, 1, (uint64_t)g_idx}; memset(&R, 0, sizeof R); R.rt.depth = g_dt; R.has_rt = 1; int ri = rts_draw(g_pr, NULL, &qi, NVN_DRAW_PRIMITIVE_QUADS);
        main_pass(); hdr_again(); ok13 = frame_close() && ok13;
        rts_mesh *q4 = NULL, *q10 = NULL; for (uint32_t k = 0; k < g_rts.meshes.cap; k++) { rts_mesh *m = g_rts.meshes.vals[k]; if (m && m->nverts == 4 && m->ib) q4 = m; if (m && m->nverts == 10 && m->ib) q10 = m; }
        const uint32_t *t4 = q4 ? q4->ib.contents : NULL, *t10 = q10 ? q10->ib.contents : NULL; uint32_t w4[6] = {0, 1, 2, 0, 2, 3}, w10b[6] = {4, 5, 6, 4, 6, 7};
        EXPECT("Q7-02 四个顶点的四边形：2 个三角形（原版也是 2 个）", 2, q4 ? q4->tris : 0); EXPECT("Q7-02 四边形索引 0,1,2,0,2,3", 1, t4 && !memcmp(t4, w4, sizeof w4));
        EXPECT("Q7-02 十个顶点（first 8）：4 个三角形、尾巴 2 个不画", 4, q10 ? q10->tris : 0); EXPECT("Q7-02 第二个四边形索引 4,5,6,4,6,7", 1, t10 && !memcmp(t10 + 6, w10b, sizeof w10b));
        EXPECT("Q7-02 不足 4 个顶点、带索引的四边形：只多了上面两个网格", 2, g_rts.nmesh - nm0); EXPECT("Q7-02 带索引的四边形：照原版处理（返回 0）", 0, ri); EXPECT("Q7-02 不足 4 个和带索引的记空绘制", 2, (long long)(g_rts.n_empty - e0)); }
      // 25. 八审 Q8-01：全亮遮罩建不出时（注入分配失败）不选 _rt 变体、照原版着色；同一帧几百次绘制只试一次分配，之后隔 RTS_LIT_RETRY 帧才再试；恢复后照常建、选变体、不压暗
      // 25. eighth review Q8-01: when the lit mask cannot be built (injected allocation failure) no _rt variant is selected and the original shading runs; hundreds of draws in one frame try the allocation once, the next try comes RTS_LIT_RETRY frames later; after recovery it is built, variants are selected, nothing is darkened
      { NvProg *vp = [NvProg new]; vp->sh[0] = g_pr->sh[0]; vp->sh[1] = [NvShader new]; vp->fs_base = vp->sh[1]; vp->fs_rt = [NvShader new]; vp->rt_tried = 1;
        #define SELECT(c, d) ({ R.rt.n = 1; R.rt.color[0] = (c); R.rt.depth = (d); g_rts.hdr_ser = (c)->serial; g_rts.ds_ser = (d)->serial; g_rts.trace_pending = 0; S.ds.dt = 1; S.color.blend_en = 0; vp->sh[1] = vp->fs_base; int dbg_ = g_rts.dbg; g_rts.dbg = 0; rts_select_fs(vp); g_rts.dbg = dbg_; vp->sh[1] == vp->fs_rt; })
        #define HOLD() (g_rts.mask_done = 1, g_rts.map_cleared = 0)   // 迟滞等待的状态：遮罩算了、画面原版，变体要绑全亮遮罩 | the hysteresis-hold state: mask traced, picture original, the variant needs the lit mask
        RTS_HYST_RESET(); UNTIL_PART(b2, 2); long long t0 = RTS_LIT_TRIES(); int nsel = 0;
        RTS_LIT_FAIL_SET(1); RTS_LIT_DROP(); HOLD();
        for (int i = 0; i < 300; i++) nsel += SELECT(&g_hdr, &g_mdepth);
        EXPECT("Q8-01 全亮遮罩建不出：300 次绘制一次都不选 _rt 变体", 0, nsel);
        EXPECT("Q8-01 全亮遮罩建不出：同一帧 300 次绘制只试分配 1 次", 1, RTS_LIT_TRIES() - t0);
        int nf = 0; for (int i = 0; i < 5; i++) { run_frame(1, b2, 2, &ok); ok13 = ok13 && ok; HOLD(); for (int k = 0; k < 50; k++) nsel += SELECT(&g_hdr, &g_mdepth); nf++; }
        EXPECT("Q8-01 之后 5 帧（各 50 次绘制）：还在等，不再试分配", 1, RTS_LIT_TRIES() - t0); EXPECT("Q8-01 之后 5 帧：仍不选变体", 0, nsel);
        for (int i = 0; i < RTS_LIT_RETRY + 2 && RTS_LIT_TRIES() - t0 < 2; i++) { run_frame(1, b2, 2, &ok); ok13 = ok13 && ok; HOLD(); nsel += SELECT(&g_hdr, &g_mdepth); nf++; }
        EXPECT("Q8-01 一直建不出：等满间隔再试第 2 次，仍不选变体", 1, RTS_LIT_TRIES() - t0 == 2 && nf >= RTS_LIT_RETRY - 6 && !nsel && !g_rts.mask_lit);   // 第 2 次失败不再记【红】（日志里只一行，见证据） | the second failure logs no red line (one line in the log, see the evidence)
        RTS_LIT_FAIL_SET(0); int built = 0; nf = 0;
        for (int i = 0; i < RTS_LIT_RETRY + 2 && !built; i++) { run_frame(1, b2, 2, &ok); ok13 = ok13 && ok; nf++; built = g_rts.mask_lit && g_rts.mask_lit.width == 640; }
        EXPECT("Q8-01 分配恢复后：等满间隔就重建了 640x360 全亮遮罩", 1, built); EXPECT("Q8-01 不是马上重试：失败后至少隔 RTS_LIT_RETRY - 6 帧", 1, nf >= RTS_LIT_RETRY - 6);
        EXPECT("Q8-01 整段只试了 3 次分配（失败 2 次 + 恢复 1 次）", 3, RTS_LIT_TRIES() - t0);
        HOLD(); int sel = SELECT(&g_hdr, &g_mdepth);
        EXPECT("Q8-01 恢复后：迟滞等待帧选上变体", 1, sel); EXPECT("Q8-01 恢复后：640x360 没有像素被压暗", 0, sel ? dark_pixels(rts_mask_tex(), 640, 360) : -1);
        #undef HOLD
        #undef SELECT
        memset(&R, 0, sizeof R); }
      EXPECT("局部回退各节 GPU 无错", 1, ok13);
      #undef UNTIL_PART
      g_lvp = NULL; RTS_PART_SET(0); }
#endif
    // 25. 系统内存告警（2026-10-28 真机内存 P1 之后）：告警的下一帧放掉网格缓存（旧 BLAS 真释放）并降一档；之前的代码告警什么都不放、档位不动
    // 25. System memory warning (after the 2026-10-28 device memory P1): the frame after a warning releases the mesh cache (the old BLAS is really freed) and steps down one level; the earlier code released nothing and kept the level
    { int a1[1] = {0}; RTS_HYST_RESET(); EXPECT("25 告警前光追生效", 1, until_applied(a1, 1));
      __weak id w_as = nil; @autoreleasepool { rts_mesh *m = mesh_by_ib(g_idx, 96); w_as = m ? m->as : nil; } int live; @autoreleasepool { live = w_as != nil; } EXPECT("25 告警前网格 0 的 BLAS 在", 1, live);
      int lv0 = g_rts.gov.lvl; mtl_note_memory_warning();
      int gone = 0; for (int i = 0; i < 100 && !gone; i++) { @autoreleasepool { frame_open(); prepass(); shadow_pass(); sh(0); main_pass(); hdr_again(); frame_close(); gone = w_as == nil; } if (!gone) usleep(20000); }
      EXPECT("25 告警后旧 BLAS 已释放（Metal 对象归零）", 1, gone); EXPECT("25 告警后降了一档", lv0 + 1, g_rts.gov.lvl); EXPECT("25 告警后光追照常（网格重建）", 1, until_applied(a1, 1)); }
#ifdef RTS_MEMW_STREAK_S
    // 26. 内存告警的其余规则：AO 放掉且不再建；10 秒内第二次 = 连续告警：最低档、网格缓存上限减半、点光遮罩放掉；迟滞（60 秒后放宽一档、之后每 30 秒一档，升不升仍由自动降档）；
    //     自动降档升不过最低档；按预算的方案（缺省放得下不动、放不下依次关 AO / 缩网格缓存 / 最低档 3）；B 级：光追关着时告警不碰光追
    // 26. The remaining memory-warning rules: AO released and not rebuilt; a second within 10 s = consecutive: lowest level, mesh cache cap halved, point masks released; hysteresis (one level loosened after 60 s, then one per 30 s, stepping up still up to the automatic downgrade);
    //     the automatic downgrade cannot step above the floor; the budget plan (default fits → unchanged, otherwise AO off / mesh cache shrunk / floor 3 in order); level B: warnings while ray tracing is off do not touch it
    { int a1[1] = {0}; uint64_t b0 = g_rts.budget0; g_rts.budget0 = g_rts.budget = 64ull << 20; g_rts.mplan = (rts_mem_plan){0}; g_rts.plan_w = 0;   // 设置值换了：方案按新设置值重算 | the configured cap changed: the plan is redone for it
      #define FULL() do { frame_open(); prepass(); shadow_pass(); sh(0); main_pass(); hdr_again(); frame_close(); } while (0)   // 帧开头的处理要有光追调用才进得去（空帧不经过模块） | the frame-start handling needs a ray-tracing call to run (an empty frame does not go through the module)
      g_rts.memw = (rts_memw){0}; g_rts.gov.lvl = 0; g_rts.ao_on = 1; rts_lvl_update();
      for (int i = 0; i < 6 && !g_rts.ao_full; i++) @autoreleasepool { frame_open(); prepass(); shadow_pass(); sh(0); main_pass(); hdr_again(); frame_close(); }
      EXPECT("26 告警前 AO 三张贴图在", 1, g_rts.ao_half && g_rts.ao_tmp && g_rts.ao_full);
      __weak id w_ao = nil; @autoreleasepool { w_ao = g_rts.ao_full; }
      mtl_note_memory_warning();
      int gone = 0; for (int i = 0; i < 100 && !gone; i++) { @autoreleasepool { frame_open(); prepass(); shadow_pass(); sh(0); main_pass(); hdr_again(); frame_close(); gone = w_ao == nil; } if (!gone) usleep(20000); }
      EXPECT("26 告警后 AO 贴图已释放", 1, gone); EXPECT("26 告警后 AO 不再建（最低第 1 档关 AO）", 1, !g_rts.ao_half && !g_rts.ao_tmp && !g_rts.ao_full && !g_rts.eff.ao);
      EXPECT("26 单次告警：最低第 1 档", 1, g_rts.memw.floor); EXPECT("26 单次告警：网格缓存上限不变", 64, (long long)(g_rts.budget >> 20));
      MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:64 height:64 mipmapped:NO]; td.storageMode = MTLStorageModePrivate;
      __weak id w_pm = nil; @autoreleasepool { g_rts.pmask = [g_dev newTextureWithDescriptor:td]; g_rts.pmaskf = [g_dev newTextureWithDescriptor:td]; w_pm = g_rts.pmaskf; }
      mtl_note_memory_warning(); gone = 0;
      for (int i = 0; i < 100 && !gone; i++) { @autoreleasepool { frame_open(); prepass(); shadow_pass(); sh(0); main_pass(); hdr_again(); frame_close(); gone = w_pm == nil; } if (!gone) usleep(20000); }
      EXPECT("26 连续告警：最低第 3 档", 3, g_rts.memw.floor); EXPECT("26 连续告警：当前第 3 档", 3, g_rts.gov.lvl); EXPECT("26 连续告警：网格缓存上限减半", 32, (long long)(g_rts.budget >> 20));
      EXPECT("26 连续告警：点光遮罩放掉（第 3 档不用点光）", 1, gone && !g_rts.pmask && !g_rts.pmaskf); EXPECT("26 连续告警后光追照常", 1, until_applied(a1, 1));
      g_rts.gov.lvl = 0; rts_lvl_update(); EXPECT("26 档位低于最低档：帧开头抬回最低档", 3, g_rts.gov.lvl);
      g_rts.gov.n = 100; g_rts.gov.fr = 1; g_rts.gov.rt = 0.1; g_rts.gov.t_under = rts_now() - 100; g_rts.gov.t_change = g_rts.gov.t_up = -1e9; uint64_t held = g_rts.n_mem_floor_held;
      rts_gov_feed(0.1, 1.0, 60); EXPECT("26 GPU 有余量也升不过最低档", 3, g_rts.gov.lvl); EXPECT("26 挡住的升档计数", 1, (long long)(g_rts.n_mem_floor_held - held));
      g_rts.memw.t_last -= 59; g_rts.memw.t_step -= 59; @autoreleasepool { FULL(); } EXPECT("26 告警后 59 秒：还不放宽", 3, g_rts.memw.floor);
      g_rts.memw.t_last -= 2; g_rts.memw.t_step -= 2; @autoreleasepool { FULL(); } EXPECT("26 告警后 61 秒：放宽到最低第 2 档", 2, g_rts.memw.floor); EXPECT("26 放宽不等于升档（升不升由自动降档）", 3, g_rts.gov.lvl);
      @autoreleasepool { FULL(); } EXPECT("26 放宽后不到 30 秒：不再放宽", 2, g_rts.memw.floor);
      g_rts.memw.t_step -= 31; @autoreleasepool { FULL(); } EXPECT("26 再过 30 秒：最低第 1 档", 1, g_rts.memw.floor);
      g_rts.memw.t_step -= 31; @autoreleasepool { FULL(); } EXPECT("26 再过 30 秒：最低第 0 档、AO / 网格缓存上限恢复", 1, g_rts.memw.floor == 0 && !g_rts.memw.ao_off && g_rts.budget == (64ull << 20));
      setenv("DM_TEST_RT_MEM_AVAIL_MB", "800", 1); EXPECT("26 测试开关给离上限 800 MB", 800, (long long)(rts_avail_bytes() >> 20)); unsetenv("DM_TEST_RT_MEM_AVAIL_MB"); EXPECT("26 Mac 读不到离上限", 0, (long long)rts_avail_bytes());
      g_rts.gov.lvl = 0; g_rts.mem_avail = 2000ull << 20; g_rts.plan_w = 0; rts_mem_replan(2852, 2006); rts_lvl_update();
      EXPECT("26 预算够（2000 MB → 192 MB）：缺省不动", 1, !g_rts.mplan.ao_off && !g_rts.mplan.lvl_floor && g_rts.budget == (64ull << 20) && g_rts.eff.ao && g_rts.gov.lvl == 0);
      g_rts.mem_avail = 400ull << 20; g_rts.plan_w = 0; rts_mem_replan(2852, 2006);
      EXPECT("26 预算 50 MB（原生 2852x2006）：关 AO", 1, g_rts.mplan.ao_off && !g_rts.eff.ao); EXPECT("26 预算 50 MB：网格缓存上限缩到 16 MB", 16, (long long)(g_rts.budget >> 20)); EXPECT("26 预算 50 MB：最低第 3 档", 3, g_rts.gov.lvl);
      EXPECT("26 预算 50 MB：收紧后估算不超预算", 1, g_rts.mplan.est <= g_rts.mem_budget);
      g_rts.mem_avail = 0; g_rts.plan_w = 0; rts_mem_replan(640, 360); g_rts.gov.lvl = 0; rts_lvl_update(); EXPECT("26 预算不限：回到缺省", 1, !g_rts.mplan.ao_off && !g_rts.mplan.lvl_floor && g_rts.gov.lvl == 0);
      g_rts.on = 0; for (int i = 0; i < 3; i++) @autoreleasepool { frame_open(); sh(0); frame_close(); }
      uint64_t n0 = g_rts.memw.n, k0 = g_rts.key_ns; mtl_note_memory_warning(); for (int i = 0; i < 3; i++) @autoreleasepool { frame_open(); sh(0); frame_close(); }
      EXPECT("26 B 级 光追关：告警不处理（计数不变）", 0, (long long)(g_rts.memw.n - n0)); EXPECT("26 B 级 光追关：不建网格、不算键", 1, g_rts.nmesh == 0 && g_rts.key_ns == k0 && !g_rts.tlas);
      g_rts.on = 1; EXPECT("26 再开后光追生效", 1, until_applied(a1, 1)); EXPECT("26 再开后不按关着时的告警降档", 1, g_rts.memw.n == n0 && g_rts.gov.lvl == 0 && g_rts.memw.floor == 0);
      g_rts.ao_on = 0; g_rts.budget0 = g_rts.budget = b0; g_rts.mplan = (rts_mem_plan){0}; g_rts.plan_w = 0; rts_lvl_update();
      #undef FULL
    }
#endif
    printf("== %d 项，失败 %d ==\n", count, fail);
    return fail != 0;
}}
#endif
