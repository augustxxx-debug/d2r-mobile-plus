// 光追阴影网格键的读取跨度（五审 Q5-01）：生产 rts_draw + 真 Metal 设备。索引 / 顶点片段放在一页内存池的末尾，紧跟一页不可访问的守护页：
// 读到已校验跨度之外 = 碰守护页 = 当场 SIGSEGV / SIGBUS（-fsanitize=address 编的那一遍同样报出）。另核：指纹只取已校验跨度（跨度外字节不同 → 同一网格）、
// 顶点范围超出内存池 / 负基顶点 / 索引数量乘法溢出 / 索引越过顶点缓冲 都不建网格。机器不支持光追（supportsRaytracing、Apple9）时跳过（退出 77）。
// Read span of the ray-traced shadow mesh key (fifth review Q5-01): production rts_draw + a real Metal device. Index / vertex fragments sit at the end of a one-page memory pool followed by an inaccessible guard page:
// reading beyond the validated span = touching the guard page = SIGSEGV / SIGBUS on the spot (also reported in the -fsanitize=address pass). Also checked: the fingerprint covers only the validated span (different bytes past it → the same mesh),
// and no mesh is created for a vertex range past the memory pool / a negative base vertex / an overflowing index count multiplication / indices past the vertex buffer. Skipped (exit 77) when the machine cannot ray trace (supportsRaytracing, Apple9).
#include "../nvn/nvn_mtl.m"
#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
#if DM_NVN_M3
int main(void) { printf("SKIP：旧接口版没有光追阴影模块\n"); return 77; }
#else
static int fail, count;
#define EXPECT(name, want, got) do { long long w_=(long long)(want), g_=(long long)(got); count++; printf("%s 期望 %lld → 真结果 %lld %s\n", name, w_, g_, w_==g_?"✓":"✗"); fail += w_ != g_; } while (0)
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ASAN 1
#endif
#endif
#ifdef RTS_HAS_NOTE_PASS
#define RTS_KEY_NS() g_rts.key_ns
#else
#define RTS_KEY_NS() 0ull   // 79d571f 没有这项统计 | 79d571f has no such counter
#endif
static sigjmp_buf g_jb; static volatile int g_in;
static void on_fault(int sig) { if (g_in) siglongjmp(g_jb, sig); signal(sig, SIG_DFL); raise(sig); }
static size_t PG; static uint8_t *g_pool, *g_guard, *g_mat, *g_cb3, *g_vtx;
static NvProg *g_pr; static tex_t g_sdepth; static uint64_t g_frame = 100;
static void setup(void) {
    PG = (size_t)getpagesize();
    if (posix_memalign((void **)&g_pool, PG, 1 << 20)) abort(); memset(g_pool, 0, 1 << 20); mtl_pool_add(g_pool, 1 << 20);
    g_guard = mmap(NULL, 2 * PG, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0); if (g_guard == MAP_FAILED) abort();
    memset(g_guard, 0, PG); if (mprotect(g_guard + PG, PG, PROT_NONE)) abort(); mtl_pool_add(g_guard, PG);   // 池 = 第一页，第二页是守护页 | pool = the first page, the second is the guard page
    g_mat = g_pool; float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; memcpy(g_mat, m, 64);
    g_cb3 = g_pool + 4096; float l[16] = {1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1}; memcpy(g_cb3 + 336, l, 64);   // 光源视投影：z 列朝上 | light view-projection: z column points up
    g_vtx = g_pool + 65536; for (int i = 0; i < 4096; i++) { int16_t *v = (int16_t *)(g_vtx + i * 8); v[0] = (int16_t)(i * 7); v[1] = (int16_t)(i * 3); v[2] = (int16_t)(-i); v[3] = 0; }
    g_pr = [NvProg new]; g_pr->sh[0] = [NvShader new]; g_pr->sh[0]->rf.nb = 1; g_pr->sh[0]->rf.b[0] = (nvn_rbind){.kind = NVN_RB_CBUF, .reg = 3};
    g_sdepth = (tex_t){.serial = 77001}; g_sdepth.d.width = g_sdepth.d.height = 256; g_sdepth.d.depth = g_sdepth.d.samples = g_sdepth.d.levels = 1; g_sdepth.d.target = NVN_TEXTURE_TARGET_2D; g_sdepth.d.format = NVN_FORMAT_DEPTH32F;
}
// 一次阴影通道的索引绘制（三角形列表、SNORM16x4、步长 8） | One indexed shadow-pass draw (triangle list, SNORM16x4, stride 8)
static int shadow_draw(int itype, int n, const void *ib, int base, const void *vb, uint64_t vsize, int fault[1]) {
    memset(&R, 0, sizeof R); R.rt.depth = &g_sdepth;
    S.nattr = 1; S.attr[0] = (va_st){.fmt = 0x2b, .offset = 0, .stream = 0}; S.stream[0] = (vs_st){8, 0}; S.vbuf[0] = (NVNbufferRange){(uint64_t)vb, vsize};
    S.ubo[0][0] = (p_bind_buf){0, 0, (uint64_t)g_mat, 64}; S.ubo[0][3] = (p_bind_buf){0, 3, (uint64_t)g_cb3, 400};
    p_draw_idx di = {NVN_DRAW_PRIMITIVE_TRIANGLES, itype, n, base, 0, 1, (uint64_t)ib};
    int r = -1; *fault = 0;
#ifndef ASAN
    struct sigaction sa = {0}, o1, o2; sa.sa_handler = on_fault; sigaction(SIGSEGV, &sa, &o1); sigaction(SIGBUS, &sa, &o2);
    int sig = sigsetjmp(g_jb, 1); if (sig) { *fault = sig; g_in = 0; } else { g_in = 1; r = rts_draw(g_pr, NULL, &di, NVN_DRAW_PRIMITIVE_TRIANGLES); g_in = 0; }
    sigaction(SIGSEGV, &o1, NULL); sigaction(SIGBUS, &o2, NULL);
#else
    r = rts_draw(g_pr, NULL, &di, NVN_DRAW_PRIMITIVE_TRIANGLES);
#endif
    return r;
}
// 只驱动阴影绘制、没有主通道，帧永远不会被判完整：开帧后直接当光追帧（生产只在光追帧算网格键、查缓存） | Only shadow draws are driven, with no main pass, so no frame is ever judged complete: treat each frame as ray-traced right after opening it (production computes the mesh key and looks up the cache only in ray-traced frames)
static void next_frame(void) { mtl_set_frame(++g_frame);
#ifdef RTS_MESH_READY
    rts_frame_begin(g_frame); g_rts.rt_frame = 1;
#endif
}
static void fill_idx(void *p, int itype, int n, uint32_t mod) { for (int k = 0; k < n; k++) { uint32_t v = (uint32_t)((k * 5 + 1) % mod); if (itype == 1) ((uint16_t *)p)[k] = (uint16_t)v; else ((uint32_t *)p)[k] = v; } }
static rts_mesh *mesh_with(uint32_t tris, uint32_t isz) { for (uint32_t i = 0; i < g_rts.meshes.cap; i++) { rts_mesh *m = g_rts.meshes.vals[i]; if (m && m->tris == tris && m->isz == isz && m->last_frame == g_rts.frame) return m; } return NULL; }

int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); setenv("DM_RT_SHADOW", "1", 1); setenv("DM_RT_SHADOW_DBG", "1", 1); setenv("DM_RT_SHADOW_PART", "0", 1); mtl_init();   // 测的是索引范围，用「整帧清图」对照模式：测试光源竖直，局部回退里不算太阳（4e92532） | this tests index ranges, in the clear-the-frame comparison mode: the test light is vertical, which partial fallback does not take as the sun (4e92532)   // rt2：测试里没有 _rt 变体，调试模式下用叠加合成（审计分支「无变体 = 叠加」的同一路径） | rt2: no _rt variants in the test; debug mode composites with the overlay (the audit branch's "no variants = overlay" path)
    if (!mtl_rt_shadow_supported()) { printf("SKIP：本机不支持光追阴影\n"); return 77; }
    setup(); int fault;
    // 1. 索引片段在池末端：16 / 32 位 × 3 / 6 / 99 个索引 | 1. Index fragment at the end of the pool: 16 / 32 bit × 3 / 6 / 99 indices
    int ns[3] = {3, 6, 99};
    for (int t = 1; t <= 2; t++) for (int j = 0; j < 3; j++) {
        int n = ns[j], isz = t == 1 ? 2 : 4; uint8_t *ib = g_guard + PG - (size_t)n * isz; fill_idx(ib, t, n, 64);
        next_frame(); int before = g_rts.nmesh; shadow_draw(t, n, ib, 0, g_vtx, 64 * 8, &fault);
        char nm[128]; snprintf(nm, sizeof nm, "%d 位 %d 个索引贴着池末端：不越界读", isz * 8, n); EXPECT(nm, 0, fault);
        snprintf(nm, sizeof nm, "%d 位 %d 个索引贴着池末端：建一个网格", isz * 8, n); EXPECT(nm, before + 1, g_rts.nmesh);
        rts_mesh *m = mesh_with((uint32_t)n / 3, (uint32_t)isz); snprintf(nm, sizeof nm, "%d 位 %d 个索引：网格三角形数", isz * 8, n); EXPECT(nm, n / 3, m ? m->tris : 0);
        if (fault) break;
    }
    // 2. 指纹只取已校验跨度：前 6 字节相同、之后不同的两份索引是同一网格；前 6 字节不同就是两个网格 | 2. The fingerprint covers only the validated span
    { uint8_t *a = g_pool + 8192, *b = g_pool + 8192 + 256; fill_idx(a, 1, 3, 64); fill_idx(b, 1, 3, 64); memset(a + 6, 0x11, 64); memset(b + 6, 0x22, 64);
      next_frame(); shadow_draw(1, 3, a, 0, g_vtx, 64 * 8, &fault); int n1 = g_rts.nmesh;
      memcpy(a, b, 6 + 64);   // 同一地址换成跨度外不同的内容（地址也进键，所以拷到同一处） | the same address with different bytes past the span (the address is part of the key, so copy in place)
      next_frame(); shadow_draw(1, 3, a, 0, g_vtx, 64 * 8, &fault); EXPECT("跨度外字节不同：命中同一网格", n1, g_rts.nmesh);
      ((uint16_t *)a)[1] ^= 1; next_frame(); shadow_draw(1, 3, a, 0, g_vtx, 64 * 8, &fault); EXPECT("跨度内索引不同：另建网格", n1 + 1, g_rts.nmesh); }
    // 3. 顶点片段在池末端（3 个顶点 = 24 字节，短于 32 字节的指纹窗口） | 3. Vertex fragment at the end of the pool (3 vertices = 24 bytes, shorter than the 32-byte window)
    { uint8_t *v = g_guard + PG - 24; for (int i = 0; i < 12; i++) ((int16_t *)v)[i] = (int16_t)(100 + i); uint16_t *ib = (uint16_t *)(g_pool + 12288); ib[0] = 0; ib[1] = 1; ib[2] = 2;
      next_frame(); int before = g_rts.nmesh; shadow_draw(1, 3, ib, 0, v, 24, &fault);
      EXPECT("顶点贴着池末端：不越界读", 0, fault); EXPECT("顶点贴着池末端：照常建网格", before + 1, g_rts.nmesh); }
    // 4. 顶点缓冲声称的大小超出内存池：不建网格（解码核会读到池外） | 4. Vertex buffer size claimed past the memory pool: no mesh (the decode kernel would read outside the pool)
    { uint8_t *v = g_guard + PG - 64; uint16_t *ib = (uint16_t *)(g_pool + 12288 + 64); ib[0] = 0; ib[1] = 5; ib[2] = 20;   // 要 21 个顶点 = 168 字节，池里只剩 64 | needs 21 vertices = 168 bytes, the pool has 64 left
      next_frame(); int before = g_rts.nmesh; shadow_draw(1, 3, ib, 0, v, 4096, &fault);
      EXPECT("顶点范围超出内存池：不建网格", before, g_rts.nmesh); EXPECT("顶点范围超出内存池：不越界读", 0, fault); }
    // 5. 负基顶点、索引数量乘法溢出、索引越过顶点缓冲：都不建网格 | 5. Negative base vertex, overflowing index count, indices past the vertex buffer: no mesh
    { uint16_t *ib = (uint16_t *)(g_pool + 12288 + 128); ib[0] = 0; ib[1] = 1; ib[2] = 2;
      next_frame(); int before = g_rts.nmesh; shadow_draw(1, 3, ib, -2, g_vtx + 64, 64 * 8, &fault); EXPECT("负基顶点：不建网格", before, g_rts.nmesh);
      next_frame(); shadow_draw(2, 0x7fffffff, ib, 0, g_vtx, 64 * 8, &fault); EXPECT("索引数量 × 4 超出内存池：不建网格", before, g_rts.nmesh); EXPECT("索引数量 × 4：不越界读", 0, fault);
      ib[4] = 0; ib[5] = 1; ib[6] = 900; next_frame(); shadow_draw(1, 3, ib + 4, 0, g_vtx, 64 * 8, &fault); EXPECT("索引 900 越过 64 个顶点：不建网格", before, g_rts.nmesh); }
    // 6. 同一地址、前 32 字节相同、后段不同（六审第 5.2 节）：索引第 50 个、顶点第 40 个改了，都不能命中旧缓存项 | 6. Same address, same first 32 bytes, different later bytes (sixth review 5.2): index 50 or vertex 40 changed must not hit the old cache entry
    { uint16_t *ib = (uint16_t *)(g_pool + 16384); uint8_t *v = g_pool + 200000; for (int k = 0; k < 99; k++) ib[k] = (uint16_t)(k % 64);
      for (int i = 0; i < 64; i++) { int16_t *q = (int16_t *)(v + i * 8); q[0] = (int16_t)(i * 11); q[1] = (int16_t)(i * 5); q[2] = (int16_t)i; q[3] = 0; }
      next_frame(); int n0 = g_rts.nmesh; shadow_draw(1, 99, ib, 0, v, 64 * 8, &fault);
      next_frame(); shadow_draw(1, 99, ib, 0, v, 64 * 8, &fault); EXPECT("内容不变：命中同一缓存项", n0 + 1, g_rts.nmesh);
      ib[50] = 63; next_frame(); shadow_draw(1, 99, ib, 0, v, 64 * 8, &fault); EXPECT("第 50 个索引变了（第 100 字节）：另建缓存项", n0 + 2, g_rts.nmesh);
      ((int16_t *)(v + 40 * 8))[1] += 7; next_frame(); shadow_draw(1, 99, ib, 0, v, 64 * 8, &fault); EXPECT("第 40 个顶点变了（第 320 字节）：另建缓存项", n0 + 3, g_rts.nmesh); }
    // 7. 键摘要的 CPU 开销（只打印）：按真机六审的量级，一帧 218 次阴影绘制 = 120 份不同几何 + 98 次两级级联重复，每份 2600 个 16 位索引、约 600 个顶点 | 7. CPU cost of the key digest (printed only): at the scale seen on device in the sixth review, 218 shadow draws per frame = 120 distinct pieces of geometry + 98 cascade repeats, 2600 16-bit indices and about 600 vertices each
    { enum { NG = 120 }; uint8_t *base = g_pool + 330000;
      for (int g = 0; g < NG; g++) { uint16_t *ib = (uint16_t *)(base + g * 5200); for (int k = 0; k < 2600; k++) ib[k] = (uint16_t)((k * 7 + g) % 600); }
      for (int w = 0; w < NG / 10 + 2; w++) { next_frame(); for (int g = 0; g < NG; g++) shadow_draw(1, 2600, base + g * 5200, 0, g_vtx, 4096 * 8, &fault); }   // 先把 120 份都建进缓存（每帧最多 12 个） | first get all 120 into the cache (at most 12 per frame)
      uint64_t t0 = bp_now(), k0 = RTS_KEY_NS(); int frames = 20;
      for (int f = 0; f < frames; f++) { next_frame(); for (int d = 0; d < 218; d++) shadow_draw(1, 2600, base + (d % NG) * 5200, 0, g_vtx, 4096 * 8, &fault); }
      printf("键摘要开销：%.1f µs / 帧（218 次绘制 = 120 份几何 + 98 次重复，每份 2600 索引 × 600 顶点，全部命中），其中算键 %.1f µs\n", (bp_now() - t0) / 1e3 / frames, (RTS_KEY_NS() - k0) / 1e3 / frames); }
    printf("== %d 项，失败 %d ==\n", count, fail);
    return fail != 0;
}}
#endif
