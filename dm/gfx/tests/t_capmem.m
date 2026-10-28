// Metal 抓帧层与常驻内存（光追内存 P1，2026-10-28）：真机 a8161cb 的 dev 包带 MetalCaptureEnabled，关着光追也每 300 帧常驻涨约 190 MB、不回落，长测十几秒被系统按内存结束。
// 本测试用生产 mtl_sub_begin / rts_draw / rts_build_and_trace / mtl_sub_commit 跑整帧（光追开、每帧真提交），自己按两种环境各起一个子进程：
//   1. 抓帧层关（缺省）：预热 300 帧之后再跑 1500 帧，常驻（phys_footprint）涨幅 < 8 MB；启动日志「Metal 抓帧层：关」
//   2. 抓帧层开（METAL_CAPTURE_ENABLED=1，等同 iOS Info.plist MetalCaptureEnabled）：同样的帧常驻涨幅 > 20 MB（证明抓帧层就是逐帧上涨的来源、所以缺省不带）；启动日志「【红】Metal 抓帧层：开」
// Metal capture layer and resident memory (ray-tracing memory P1, 2026-10-28): the a8161cb dev package on the device carried MetalCaptureEnabled, and even with ray tracing off resident memory grew about 190 MB per 300 frames and never came back; the long test was ended by the system for memory after a dozen seconds.
// This test runs whole frames through the production mtl_sub_begin / rts_draw / rts_build_and_trace / mtl_sub_commit (ray tracing on, every frame really submitted), starting one child process per environment:
//   1. capture layer off (default): after 300 warm-up frames, 1500 more frames grow the resident size (phys_footprint) by < 8 MB; boot log "Metal 抓帧层：关"
//   2. capture layer on (METAL_CAPTURE_ENABLED=1, same as the iOS Info.plist MetalCaptureEnabled): the same frames grow it by > 20 MB (showing the capture layer is the per-frame growth, hence off by default); boot log "【红】Metal 抓帧层：开"
#include "../nvn/nvn_mtl.m"
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/wait.h>
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
extern char **environ;
#if DM_NVN_M3
int main(void) { printf("SKIP：旧接口版没有光追阴影模块\n"); return 77; }
#else
static uint8_t *g_pool, *g_mat, *g_cb3, *g_cam, *g_vtx, *g_idx;
static NvProg *g_pr; static tex_t g_sdepth, g_mdepth, g_hdr; static uint64_t g_frame = 1000;
static void setup(void) {   // 同 t_rts_gate 的合成场景 | the same synthetic scene as t_rts_gate
    size_t PG = (size_t)getpagesize();
    if (posix_memalign((void **)&g_pool, PG, 1 << 20)) abort(); memset(g_pool, 0, 1 << 20); mtl_pool_add(g_pool, 1 << 20);
    g_mat = g_pool; float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; memcpy(g_mat, m, 64);
    g_cb3 = g_pool + 4096; float l[16] = {1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1}; memcpy(g_cb3 + 336, l, 64);
    g_cam = g_pool + 8192; float c[16] = {0.5f, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 0.01f, 0, 0, 0, 0.5f, 1}; memcpy(g_cam + 336, c, 64);
    g_vtx = g_pool + 65536; for (int i = 0; i < 4096; i++) { int16_t *v = (int16_t *)(g_vtx + i * 8); v[0] = (int16_t)((i % 64) * 400); v[1] = (int16_t)((i / 64) * 400); v[2] = (int16_t)(i % 7 * 100); v[3] = 0; }
    g_idx = g_pool + 131072; for (uint32_t i = 0; i < 3000; i++) ((uint16_t *)g_idx)[i] = (uint16_t)(((i * 2654435761u) >> 13) % 512);
    g_pr = [NvProg new]; g_pr->sh[0] = [NvShader new]; g_pr->sh[0]->rf.nb = 1; g_pr->sh[0]->rf.b[0] = (nvn_rbind){.kind = NVN_RB_CBUF, .reg = 3};
    g_sdepth = (tex_t){.serial = 79101}; g_sdepth.d.width = g_sdepth.d.height = 256; g_sdepth.d.depth = g_sdepth.d.samples = g_sdepth.d.levels = 1; g_sdepth.d.target = NVN_TEXTURE_TARGET_2D; g_sdepth.d.format = NVN_FORMAT_DEPTH32F;
    g_mdepth = (tex_t){.serial = 79102}; g_mdepth.d.width = 640; g_mdepth.d.height = 360; g_mdepth.d.depth = g_mdepth.d.samples = g_mdepth.d.levels = 1; g_mdepth.d.target = NVN_TEXTURE_TARGET_2D; g_mdepth.d.format = NVN_FORMAT_DEPTH32F_STENCIL8;
    g_hdr = g_mdepth; g_hdr.serial = 79103; g_hdr.d.format = NVN_FORMAT_R11G11B10F;
}
static void shadow_pass(void) {
    rp_end(); id<MTLTexture> t = tex_mtl(&g_sdepth);
    RPD *rp = [RPD new]; rp.depthAttachment.texture = t; rp.depthAttachment.loadAction = MTLLoadActionClear; rp.depthAttachment.clearDepth = 0.25; rp.depthAttachment.storeAction = MTLStoreActionStore;
    RENC en = [g_cb renderCommandEncoderWithDescriptor:rp]; m4_end(en);
    memset(&R, 0, sizeof R); R.rt.depth = &g_sdepth; R.has_rt = 1; rp.depthAttachment.clearDepth = 1.0; rts_note_pass(rp);
}
static void sdraw(int i) {
    memset(&R, 0, sizeof R); R.rt.depth = &g_sdepth; R.has_rt = 1;
    S.nattr = 1; S.attr[0] = (va_st){.fmt = 0x2b, .offset = 0, .stream = 0}; S.stream[0] = (vs_st){8, 0}; S.vbuf[0] = (NVNbufferRange){(uint64_t)g_vtx, 4096 * 8};
    S.ubo[0][0] = (p_bind_buf){0, 0, (uint64_t)g_mat, 64}; S.ubo[0][3] = (p_bind_buf){0, 3, (uint64_t)g_cb3, 400};
    p_draw_idx di = {NVN_DRAW_PRIMITIVE_TRIANGLES, 1, 48, 0, 0, 1, (uint64_t)(g_idx + (size_t)i * 128)}; rts_draw(g_pr, NULL, &di, NVN_DRAW_PRIMITIVE_TRIANGLES);
}
static void set_rt(const tex_t *c, const tex_t *d) { p_set_rt r = {0}; r.n = 1; r.color[0] = c; r.depth = d; R.rt = r; R.has_rt = 1; rts_set_rt(&r); }
static int frame(void) {   // 一整帧：预渲染（相机）→ 阴影通道 + 8 次阴影绘制 → 主通道（光追在这里建 / 追）→ 提交并等完 | one whole frame: prepass (camera) → shadow pass + 8 shadow draws → main pass (ray tracing builds / traces here) → submit and wait
    @autoreleasepool {
        mtl_set_frame(++g_frame); mtl_sub_begin();
        memset(&R, 0, sizeof R); R.rt.depth = &g_mdepth; S.ubo[0][3] = (p_bind_buf){0, 3, (uint64_t)g_cam, 400}; p_draw d = {NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3}; rts_draw(g_pr, &d, NULL, NVN_DRAW_PRIMITIVE_TRIANGLES);
        shadow_pass(); for (int k = 0; k < 8; k++) sdraw(k); set_rt(&g_hdr, &g_mdepth); set_rt(&g_hdr, NULL);
        uint64_t f = mtl_sub_commit(); return mtl_wait(f, 10000000000ull) && !mtl_last_commit_error();
    }
}
static long long fp_mb(void) { task_vm_info_data_t v; mach_msg_type_number_t c = TASK_VM_INFO_COUNT; return task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&v, &c) ? -1 : (long long)(v.phys_footprint >> 20); }
static int child(void) {
    setvbuf(stdout, NULL, _IONBF, 0); unsetenv("DM_RT_SHADOW"); setenv("DM_RT_SHADOW_DBG", "1", 1); setenv("DM_RT_SHADOW_PART", "0", 1); mtl_init();
    if (!mtl_rt_shadow_supported()) { printf("SKIP：本机不支持光追阴影\n"); return 77; }
    setup(); rts_on(); g_rts.on = 1;
    int ok = 1; for (int i = 0; i < 300; i++) ok &= frame();
    long long a = fp_mb(); for (int i = 0; i < 1500; i++) ok &= frame(); long long b = fp_mb();
    printf("CHILD traced=%llu gpu_ok=%d fp0=%lld fp1=%lld growth=%lld\n", (unsigned long long)g_rts.n_traced, ok, a, b, b - a);
    return 0;
}
// 子进程跑一遍，从输出里取涨幅 / 追踪帧数 / 抓帧层那一行 | run one child and take growth / traced frames / the capture-layer line from its output
static int run_child(int cap, long long *growth, unsigned long long *traced, int *line_on, int *line_off) {
    char exe[4096]; uint32_t n = sizeof exe; if (_NSGetExecutablePath(exe, &n)) return -1;
    int pfd[2]; if (pipe(pfd)) return -1;
    posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa); posix_spawn_file_actions_adddup2(&fa, pfd[1], 1); posix_spawn_file_actions_adddup2(&fa, pfd[1], 2); posix_spawn_file_actions_addclose(&fa, pfd[0]);
    if (cap) setenv("METAL_CAPTURE_ENABLED", "1", 1); else unsetenv("METAL_CAPTURE_ENABLED");
    char *argv[] = {exe, "child", NULL}; pid_t pid; int r = posix_spawn(&pid, exe, &fa, NULL, argv, environ); unsetenv("METAL_CAPTURE_ENABLED");
    posix_spawn_file_actions_destroy(&fa); close(pfd[1]); if (r) { close(pfd[0]); return -1; }
    FILE *f = fdopen(pfd[0], "r"); char line[4096]; int ok = 0, skip = 0; *line_on = *line_off = 0;
    while (fgets(line, sizeof line, f)) {
        if (strstr(line, "【红】Metal 抓帧层：开")) (*line_on)++; else if (strstr(line, "Metal 抓帧层：关")) (*line_off)++;
        if (strstr(line, "SKIP")) skip = 1;
        int g; if (sscanf(line, "CHILD traced=%llu gpu_ok=%d fp0=%*lld fp1=%*lld growth=%lld", traced, &g, growth) == 3) ok = g; }
    fclose(f); int st = 0; waitpid(pid, &st, 0);
    if (skip || (WIFEXITED(st) && WEXITSTATUS(st) == 77)) return 77;
    return WIFEXITED(st) && WEXITSTATUS(st) == 0 && ok ? 0 : -1;
}
static int fail, count;
#define EXPECT(name, want, got) do { long long w_=(long long)(want), g_=(long long)(got); count++; printf("%s 期望 %lld → 真结果 %lld %s\n", name, w_, g_, w_==g_?"✓":"✗"); fail += w_ != g_; } while (0)
int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "child")) { @autoreleasepool { return child(); } }
    setvbuf(stdout, NULL, _IONBF, 0);
    long long g0 = 0, g1 = 0; unsigned long long t0 = 0, t1 = 0; int on0, off0, on1, off1;
    int r0 = run_child(0, &g0, &t0, &on0, &off0); if (r0 == 77) { printf("SKIP：本机不支持光追阴影\n"); return 77; }
    int r1 = run_child(1, &g1, &t1, &on1, &off1);
    printf("抓帧层关：1500 帧常驻涨 %lld MB（追踪 %llu 帧）；抓帧层开：涨 %lld MB（追踪 %llu 帧）\n", g0, t0, g1, t1);
    EXPECT("抓帧层关：子进程跑完、GPU 无错", 0, r0); EXPECT("抓帧层关：每帧都光追", 1, t0 >= 1700);
    EXPECT("抓帧层关：1500 帧常驻涨幅 < 8 MB", 1, g0 < 8); EXPECT("抓帧层关：启动日志「Metal 抓帧层：关」", 1, off0 == 1 && on0 == 0);
    EXPECT("抓帧层开：子进程跑完、GPU 无错", 0, r1); EXPECT("抓帧层开：每帧都光追", 1, t1 >= 1700);
    EXPECT("抓帧层开：1500 帧常驻涨幅 > 20 MB（逐帧上涨的来源）", 1, g1 > 20); EXPECT("抓帧层开：启动日志「【红】Metal 抓帧层：开」", 1, on1 == 1 && off1 == 0);
    printf("%s：%d 项，失败 %d\n", fail ? "FAIL" : "PASS", count, fail);
    return fail ? 1 : 0;
}
#endif
