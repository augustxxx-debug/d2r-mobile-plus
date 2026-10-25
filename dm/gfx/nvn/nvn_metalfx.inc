// 原版 ps_upsample_p 的 3D 图像与屏幕遮罩诊断；只读取绑定与度量，不写游戏对象。
// Inspect the original ps_upsample_p 3D image and screen mask; read bindings/metrics only.
#include "nvn_metalfx_policy.h"
static _Atomic int g_metalfx_active;
static _Atomic float g_metalfx_auto_scale;
__attribute__((weak)) int dm_3d_profile_get(void) { return 4; }
static int metalfx_profile(void) {
    const char *e = getenv("DM_METALFX");
    if (e) { if (!strcmp(e,"0") || !strcmp(e,"off") || !strcmp(e,"probe")) return 4;
        if (!strcmp(e,"auto")) return 0; if (!strcmp(e,"eco")) return 1; if (!strcmp(e,"quality")) return 3; if (!strcmp(e,"balanced") || !strcmp(e,"1")) return 2; }
    return dm_3d_profile_get();
}
int mtl_metalfx_active(void) { return atomic_load(&g_metalfx_active) && metalfx_profile() != 4; }
float mtl_metalfx_scale_hint(void) {
    if (!mtl_metalfx_active()) return 0;
    int mode = metalfx_profile();
    return mode == 0 ? atomic_load(&g_metalfx_auto_scale) : nvn_metalfx_scale(mode,0,0,0);
}
static int metalfx_draw(NvProg *pr, const p_draw *dw, const p_draw_idx *di) {
    static int probe = -1;
    if (probe < 0) { const char *e = getenv("DM_METALFX"); probe = e && !strcmp(e, "probe"); }
    if (!pr || !pr->sh[1] || strncmp(pr->sh[1]->key, "52aac1f2306b8fc5", 16)) return 0;
    static int count;
    if (probe && count++ < 3) {
        NvShader *f = pr->sh[1];
        MLOG("MetalFX probe: target %ux%u n=%d enc=%d draw=%d indexed=%d", R.rt.color[0] ? R.rt.color[0]->d.width : 0, R.rt.color[0] ? R.rt.color[0]->d.height : 0, R.rt.n, g_enc != nil, dw ? dw->count : 0, di != NULL);
        for (int i = 0; i < f->rf.nb; i++) { nvn_rbind *b = &f->rf.b[i];
            if (b->kind == NVN_RB_TEX && b->reg < 128) {
                uint64_t h = S.tex[1][b->reg]; void *view = NULL; tex_fast_rid((int)(h & 0xfffff), &view);
                id<MTLTexture> t = (__bridge id<MTLTexture>)view;
                MLOG("MetalFX probe: texture reg=%d size=%lux%lu fmt=%lu usage=%lu", b->reg, (unsigned long)t.width, (unsigned long)t.height, (unsigned long)t.pixelFormat, (unsigned long)t.usage);
            } else if (b->kind == NVN_RB_CBUF) {
                float v[12] = {0}; int ok = 1;
                for (int j = 0; j < 12; j++) ok &= ubo_float(1, (uint16_t)((b->reg << 8) | j), &v[j]);
                MLOG("MetalFX probe: cbuf reg=%d ok=%d uv=%.5f,%.5f step=%.5f,%.5f sharp=%.5f clamp=%.5f,%.5f,%.5f,%.5f", b->reg, ok, v[0], v[1], v[4], v[5], v[6], v[8], v[9], v[10], v[11]);
            }
        }
    }
    if (probe || metalfx_profile() == 4 || !R.has_rt || R.rt.n != 1 || !R.rt.color[0] || g_enc || g_defer_replaying || g_defer.active) return 0;
    static int supported = -1; if (supported < 0) { supported = [NvMetalFX supported:g_dev]; MLOG("MetalFX spatial support: %d", supported); }
    if (!supported) return 0;
    uint64_t serial; int level, slice; rt_sub(&R.rt, 0, &serial, &level, &slice);
    if (level || slice || !serial) return 0;
    id<MTLTexture> dst = tex_mtl(R.rt.color[0]), src = nil, mask = nil;
    NvShader *f = pr->sh[1]; float uv[2] = {0}; int uok = 0;
    for (int i = 0; i < f->rf.nb; i++) { nvn_rbind *b = &f->rf.b[i];
        if (b->kind == NVN_RB_TEX && b->reg < 128) { void *view = NULL; tex_fast_rid((int)(S.tex[1][b->reg] & 0xfffff), &view); if (b->reg == 10) src = (__bridge id<MTLTexture>)view; else if (b->reg == 11) mask = (__bridge id<MTLTexture>)view; }
        if (b->kind == NVN_RB_CBUF) uok = ubo_float(1,(uint16_t)(b->reg<<8),&uv[0]) && ubo_float(1,(uint16_t)((b->reg<<8)|1),&uv[1]);
    }
    // 不认识的视图／非整屏绘制沿用原通道；在编码前完整检查，失败不丢原绘制。
    // Unknown views or partial draws retain the original pass; validate before encoding so failures never drop it.
    if (!src || !mask || !dst || !uok || fabsf(uv[0]-1) > .0001f || fabsf(uv[1]-1) > .0001f || src.width >= dst.width || src.height >= dst.height || src == dst || mask == dst || src.textureType != MTLTextureType2D || mask.textureType != MTLTextureType2D || src.sampleCount != 1 || dst.sampleCount != 1) return 0;
    if (S.has_vp && (S.vp[0][0] != 0 || S.vp[0][1] != 0 || S.vp[0][2] != dst.width || S.vp[0][3] != dst.height)) return 0;
    if (R.has_sc && (R.sc.x > 0 || R.sc.y > 0 || R.sc.w < (int)dst.width || R.sc.h < (int)dst.height)) return 0;
    static NvMetalFX *effect; static int failed;
    if (failed) return 0;
    if (!effect || ![effect matchesInput:src output:dst]) {
        NvMetalFX *next = [[NvMetalFX alloc] initWithDevice:g_dev input:src output:dst];
        if (!next) { failed = 1; atomic_store(&g_metalfx_active,0); MLOG("MetalFX initialization unavailable; keeping original upscale"); return 0; }
        for (id<MTLResource> resource in effect.resources) defer_obj_release(resource);
        if (effect) defer_obj_unref(effect);
        effect = next; for (id<MTLResource> resource in effect.resources) res_add(resource);
        MLOG("MetalFX spatial ready: %lux%lu -> %lux%lu (3D only, original mask retained)",(unsigned long)src.width,(unsigned long)src.height,(unsigned long)dst.width,(unsigned long)dst.height);
    }
    rp_end(); if (g_blit) { enc_b_end(g_blit); g_blit = nil; } ser_gate();
    if (![effect encode:g_cb input:src contentWidth:src.width contentHeight:src.height mask:mask output:dst]) return 0;
    R.clear_c[0] = 0; args_dirty_all(); g_pso_dirty = g_dss_dirty = 1;
    atomic_store(&g_metalfx_active,1);
    static nvn_metalfx_auto_state auto_state;
    static uint64_t health_ns; static int health_mode = -1;
    uint64_t health_now = clock_gettime_nsec_np(CLOCK_MONOTONIC);
    int mode = metalfx_profile();
    if (mode != health_mode) { auto_state = (nvn_metalfx_auto_state){0}; health_ns = 0; health_mode = mode; }
    if (!health_ns || health_now - health_ns >= 1000000000ull) {
        health_ns = health_now;
        if (mode == 0) atomic_store(&g_metalfx_auto_scale,
            nvn_metalfx_auto_step(&auto_state, (unsigned)(dst.width*dst.height),
                (int)NSProcessInfo.processInfo.thermalState,
                NSProcessInfo.processInfo.lowPowerModeEnabled, health_now));
    }
    return 1;
}
