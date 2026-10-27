// ---------- DM_NVN_RTCAP=<目录>:<起始帧>：光追评估用的一帧几何抓取（评估样机，正式包不开） ----------
// 从起始帧起，每帧的每个绘制在真正编码前记一行 draws.tsv，并把索引片段、属性 0 所在顶点流的片段、顶点阶段 cb0（物体→世界矩阵）与 cb3（相机 / 光源视投影）
// 的前几百字节各存一个小文件；碰到含 ≥ 50 个阴影通道绘制的整帧后停止。离线由 dm/gfx/tools/rt_proto/cap2scene.py 转成世界空间三角形。
// 只读内存池（Shared），不改任何绘制；没有处理 GPU 时间线上的待决写入（pend_hits），抓到的常量缓冲可能是上一帧的值——评估够用。
// ---------- DM_NVN_RTCAP=<dir>:<start frame>: one-frame geometry capture for the ray-tracing evaluation (prototype only, never on in release builds) ----------
// From the start frame on, every draw of every frame writes one draws.tsv row right before it is encoded, plus small files holding the index slice, the slice of the
// vertex stream that carries attribute 0, and the first few hundred bytes of the vertex-stage cb0 (object→world) and cb3 (camera / light view-projection).
// Stops after a whole frame with ≥ 50 shadow-pass draws. dm/gfx/tools/rt_proto/cap2scene.py turns it into world-space triangles offline.
// Reads the (Shared) memory pools only and changes no draw; pending GPU-timeline writes (pend_hits) are not handled, so a captured constant block may hold the previous frame's values — fine for an evaluation.
static struct { int on; char dir[512]; uint64_t frame, cur; FILE *tsv; int n, shadow_n; } g_rtcap = {.on = -1};
static const uint8_t *rtcap_mem(uint64_t addr, uint64_t len) {
    void *m; uint64_t o; if (!addr || !len || !mtl_addr(addr, len, &m, &o)) return NULL;
    const uint8_t *b = [(__bridge id<MTLBuffer>)m contents]; return b ? b + o : NULL;
}
static void rtcap_blob(const char *name, const uint8_t *p, uint64_t n) {
    char path[640]; snprintf(path, sizeof path, "%s/f%llu/%s", g_rtcap.dir, (unsigned long long)g_rtcap.cur, name);
    FILE *f = fopen(path, "wb"); if (f) { if (p && n) fwrite(p, 1, n, f); fclose(f); }
}
static void rtcap_frame_end(void) {
    if (g_rtcap.tsv) { fclose(g_rtcap.tsv); g_rtcap.tsv = NULL; }
    MLOG("DM_NVN_RTCAP：帧 %llu 抓完，绘制 %d（阴影通道 %d）%s", (unsigned long long)g_rtcap.cur, g_rtcap.n, g_rtcap.shadow_n, g_rtcap.shadow_n >= 50 ? "，停止" : "，下一帧继续");
    if (g_rtcap.shadow_n >= 50) g_rtcap.on = 0;
}
static void rtcap_draw(NvProg *pr, const p_draw *dw, const p_draw_idx *di, int prim) {
    if (g_rtcap.on < 0) {
        g_rtcap.on = 0; const char *e = getenv("DM_NVN_RTCAP"); const char *c = e ? strrchr(e, ':') : NULL;
        if (!e || !c || c == e) return;
        snprintf(g_rtcap.dir, sizeof g_rtcap.dir, "%.*s", (int)(c - e), e); g_rtcap.frame = strtoull(c + 1, NULL, 10);
        mkdir(g_rtcap.dir, 0755); g_rtcap.on = 1; MLOG("DM_NVN_RTCAP：从帧 %llu 起抓几何到 %s", (unsigned long long)g_rtcap.frame, g_rtcap.dir);
    }
    if (!g_rtcap.on || !pr) return;
    uint64_t f = atomic_load(&g_cur_frame); if (f < g_rtcap.frame) return;
    if (f != g_rtcap.cur) {
        if (g_rtcap.tsv) rtcap_frame_end();
        if (!g_rtcap.on) return;
        g_rtcap.cur = f; g_rtcap.n = g_rtcap.shadow_n = 0;
        char path[640]; snprintf(path, sizeof path, "%s/f%llu", g_rtcap.dir, (unsigned long long)f); mkdir(path, 0755);
        snprintf(path, sizeof path, "%s/f%llu/draws.tsv", g_rtcap.dir, (unsigned long long)f); g_rtcap.tsv = fopen(path, "w");
        if (!g_rtcap.tsv) { g_rtcap.on = 0; return; }
        fprintf(g_rtcap.tsv, "idx\tpass\tshadow\tncol\tcfmt0\tdfmt\ttw\tth\tvs\tfs\tprim\tinst\tmode\titype\tcount\tfirst\tbase_vertex\tvmin\tvmax\ta0fmt\ta0off\ta0stream\tstride\tdivisor\tvb_addr\tvb_size\tnattr\tib_addr\tcb0_addr\tcb0_size\tcb3_addr\tcb3_size\tcb2_addr\tcb2_size\n");
    }
    __unsafe_unretained NvShader *vs = pr->sh[0], *fs = pr->sh[1]; if (!vs) return;
    int shadow = !R.rt.n && R.rt.depth && R.rt.depth->d.width == R.rt.depth->d.height;
    int idx = g_rtcap.n++; if (shadow) g_rtcap.shadow_n++;
    int n = dw ? dw->count : di->count, itype = dw ? 0 : di->itype, isz = itype == 1 ? 2 : itype == 2 ? 4 : 0;
    int64_t vmin = 0, vmax = -1; char nm[64];
    if (dw) { vmin = dw->first; vmax = (int64_t)dw->first + n - 1; }
    else if (isz && n > 0) {
        const uint8_t *ib = rtcap_mem(di->ibuf, (uint64_t)n * isz);
        if (ib) {
            snprintf(nm, sizeof nm, "d%d_ib.bin", idx); rtcap_blob(nm, ib, (uint64_t)n * isz);
            vmin = INT64_MAX; vmax = -1;
            for (int k = 0; k < n; k++) { int64_t v = isz == 2 ? ((const uint16_t *)ib)[k] : ((const uint32_t *)ib)[k]; if (v < vmin) vmin = v; if (v > vmax) vmax = v; }
            vmin += di->base_vertex; vmax += di->base_vertex;
        }
    }
    va_st at = S.nattr > 0 ? S.attr[0] : (va_st){0}; vs_st sm = at.stream < 16 ? S.stream[at.stream] : (vs_st){0}; NVNbufferRange vb = at.stream < 16 ? S.vbuf[at.stream] : (NVNbufferRange){0};
    if (vmax >= vmin && vmin >= 0 && sm.stride && vb.address) {
        uint64_t o0 = (uint64_t)vmin * sm.stride, o1 = (uint64_t)(vmax + 1) * sm.stride; if (o1 > vb.size) o1 = vb.size;
        if (o1 > o0) { const uint8_t *p = rtcap_mem(vb.address + o0, o1 - o0); if (p) { snprintf(nm, sizeof nm, "d%d_vb.bin", idx); rtcap_blob(nm, p, o1 - o0); } }
    }
    const p_bind_buf *c0 = &S.ubo[0][0], *c3 = &S.ubo[0][3], *c2 = &S.ubo[0][2];
    { uint64_t l = c0->size < 1024 ? c0->size : 1024; const uint8_t *p = rtcap_mem(c0->addr, l); if (p) { snprintf(nm, sizeof nm, "d%d_cb0.bin", idx); rtcap_blob(nm, p, l); } }
    { uint64_t l = c3->size < 1024 ? c3->size : 1024; const uint8_t *p = rtcap_mem(c3->addr, l); if (p) { snprintf(nm, sizeof nm, "d%d_cb3.bin", idx); rtcap_blob(nm, p, l); } }
    fprintf(g_rtcap.tsv, "%d\t%llu\t%d\t%d\t%u\t%u\t%lu\t%lu\t%.16s\t%.16s\t%d\t%d\t%c\t%d\t%d\t%d\t%d\t%lld\t%lld\t%#x\t%u\t%u\t%u\t%u\t%#llx\t%llu\t%d\t%#llx\t%#llx\t%llu\t%#llx\t%llu\t%#llx\t%llu\n",
        idx, (unsigned long long)g_stat_pass, shadow, R.rt.n, g_pass.cfmt[0], g_pass.dfmt, (unsigned long)g_pass.tw, (unsigned long)g_pass.th, vs->key, fs ? fs->key : "-", prim, dw ? 1 : (di->instances ? di->instances : 1),
        dw ? 'A' : 'I', itype, n, dw ? dw->first : 0, dw ? 0 : di->base_vertex, (long long)vmin, (long long)vmax, at.fmt, at.offset, at.stream, sm.stride, sm.divisor,
        (unsigned long long)vb.address, (unsigned long long)vb.size, S.nattr, (unsigned long long)(dw ? 0 : di->ibuf), (unsigned long long)c0->addr, (unsigned long long)c0->size,
        (unsigned long long)c3->addr, (unsigned long long)c3->size, (unsigned long long)c2->addr, (unsigned long long)c2->size);
}
