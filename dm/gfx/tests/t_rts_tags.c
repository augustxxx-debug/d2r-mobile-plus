// 本帧实例去重集合的单测（纯 C，不碰 Metal）：怪群场景的绘制次序 —— 同一种怪的多份实例，两级级联各画一次，
// 加上预渲染，一帧 2600 次登记、1070 个不同实例，其中少量绘制会中途退出（网格未就绪 / 槽满），没有登记实例。
// 对照朴素线性扫描（今天生产代码的做法）逐次核对判定完全一致，并钉住「每次登记的工作量有上限」——
// 朴素扫描是 O(登记数 × 实例数)，怪群时每帧上百万次比较。
// Unit test of the per-frame instance de-duplication set (pure C, no Metal): the draw pattern of a monster swarm - many units of the
// same type, drawn once per cascade plus the depth prepass: 2600 registrations and 1070 distinct instances per frame, with a few draws
// bailing out half-way (mesh not ready / no slot) without registering an instance. Decisions are checked one by one against the naive
// linear scan (what production does today), and the work per registration is pinned: the naive scan is O(draws x instances), i.e. over
// a million comparisons per frame in a swarm.
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../nvn/nvn_rtshadow_util.h"

#define INST 1070      // 本帧不同实例（17 Pro 血沼怪群实测 1070） | distinct instances this frame (1070 measured on the 17 Pro blood-moor swarm)
#define DRAWS 2600     // 本帧登记次数（含两级级联与预渲染） | registrations this frame (both cascades and the prepass)
#define FRAMES 120
#define PROBE_BUDGET 4 // 每次登记允许的平均探测数 | average probes allowed per registration

static uint64_t rng = 88172645463325252ull;
static uint64_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

// 生产代码今天的样子：扫这一帧已登记的实例表（只有真登记了的实例在表里） | what production does today: scan this frame's table of instances (only instances that really registered are in it)
typedef struct { uint64_t tag[INST + 8]; int n; uint64_t cmps; } naive;
static int naive_has(naive *s, uint64_t tag) {
    for (int i = 0; i < s->n; i++) { s->cmps++; if (s->tag[i] == tag) return 1; }
    return 0;
}
static void naive_add(naive *s, uint64_t tag) { if (s->n < INST + 8) s->tag[s->n++] = tag; }
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

#define MAXD 2700
static uint64_t stream[FRAMES][MAXD]; static int drop[FRAMES][MAXD], expect[FRAMES][MAXD], ndrawn[FRAMES];

int main(void) {
    // 一帧的登记序列：先 1070 个新实例，再让其中约一半被第二级级联 / 预渲染重复登记一次（次序打乱），
    // 夹少量每帧新出现的实例（怪群逐批刷怪）；另有约 3% 的绘制中途退出、没有登记实例
    // one frame's registrations: 1070 new instances, then about half of them drawn a second time by the other cascade / the prepass in a
    // shuffled order, plus a few instances new this frame; about 3% of the draws bail out without registering an instance
    for (int f = 0; f < FRAMES; f++) {
        static uint64_t tags[INST]; int nd = 0;
        for (int i = 0; i < INST; i++) tags[i] = (uint64_t)(i < INST - 8 ? i : rnd()) * 0x9E3779B97F4A7C15ull ^ (0x1234 + f);
        for (int i = 0; i < INST; i++) { stream[f][nd] = tags[i]; drop[f][nd] = (rnd() % 100) < 3; nd++; }
        for (int i = 0; i < (INST * 3) / 2 && nd < DRAWS; i++) { int k = (int)(rnd() % INST); stream[f][nd] = tags[k]; drop[f][nd] = 0; nd++; }
        for (; nd < DRAWS; nd++) { stream[f][nd] = tags[rnd() % INST]; drop[f][nd] = (rnd() % 100) < 3; }
        ndrawn[f] = nd;
    }
    // 1. 判定必须与朴素扫描逐次相同：重复 = 1，新登记 / 中途退出 = 0；中途退出的标签不许留在集合里
    // decisions must match the naive scan registration by registration: duplicate = 1, new / bailed out = 0; a bailed-out tag must not stay in the set
    int bad = 0; uint64_t max_probe_frame = 0; int probes_bad = 0;
    for (int f = 0; f < FRAMES; f++) {
        naive ref = {0}; rts_tagset ts; memset(&ts, 0, sizeof ts);
        rts_tagset_frame(&ts);
        uint64_t probes0 = ts.probes;
        for (int i = 0; i < ndrawn[f]; i++) {
            uint64_t tag = stream[f][i];
            int want = naive_has(&ref, tag) ? 1 : 0;
            int got = rts_tagset_has(&ts, tag);
            if (got < 0) got = 0;   // 集合不可用时生产代码回退到同一张表 | production falls back to the same table when the set is unusable
            expect[f][i] = want;
            if (got != want) { if (!bad) printf("第 %d 帧第 %d 次登记：期望 %d，实得 %d\n", f, i, want, got); bad++; }
            if (!want && !drop[f][i]) { naive_add(&ref, tag); rts_tagset_add(&ts, tag); }
        }
        if (ts.probes - probes0 > max_probe_frame) max_probe_frame = ts.probes - probes0;
        if (ts.probes - probes0 > (uint64_t)ndrawn[f] * PROBE_BUDGET) probes_bad++;
    }
    printf("判定一致：%s（不一致 %d 次，含中途退出的标签不留在集合里）\n", bad ? "否 ✗" : "是 ✓", bad);
    // 2. 中途退出这一条单独再核一遍：同一标签先退出、后正常绘制，第二次必须是「新登记」
    // the bail-out rule on its own: the same tag bails out first, then draws normally - the second time must count as new
    { naive ref = {0}; rts_tagset ts; memset(&ts, 0, sizeof ts); rts_tagset_frame(&ts);
      uint64_t t = 0xabcdefull; int a = rts_tagset_has(&ts, t), c = rts_tagset_has(&ts, t), d = rts_tagset_has(&ts, t);
      (void)ref; rts_tagset_add(&ts, t); int e = rts_tagset_has(&ts, t);
      printf("先退出后登记：%d %d %d → 登记后 %d（期望 0 0 0 → 1）%s\n", a, c, d, e, (a || c || d || !e) ? "✗" : "✓");
      bad += (a || c || d || !e); }
    printf("每帧探测量峰值 %llu（上限 %d，超出帧数 %d）\n", (unsigned long long)max_probe_frame, DRAWS * PROBE_BUDGET, probes_bad);
    // 3. 同一条登记序列上量朴素扫描的耗时，作对照（生产代码改前 / 改后）
    // time the naive scan on the same registration stream as a reference (production before / after)
    double t0 = now_ms();
    for (int f = 0; f < FRAMES; f++) { naive n2 = {0}; for (int i = 0; i < ndrawn[f]; i++) { if (naive_has(&n2, stream[f][i])) continue; if (!drop[f][i]) naive_add(&n2, stream[f][i]); } }
    double t_naive = (now_ms() - t0) / FRAMES;
    double t1 = now_ms();
    for (int f = 0; f < FRAMES; f++) { rts_tagset ts; memset(&ts, 0, sizeof ts); rts_tagset_frame(&ts); for (int i = 0; i < ndrawn[f]; i++) { if (rts_tagset_has(&ts, stream[f][i])) continue; if (!drop[f][i]) rts_tagset_add(&ts, stream[f][i]); } }
    double t_set = (now_ms() - t1) / FRAMES;
    printf("每帧：朴素扫描 %.3f ms；去重集合 %.3f ms（%ux）\n", t_naive, t_set, t_set > 0 ? (int)(t_naive / t_set) : 0);
    // 4. 满载契约：载入率到 3/4 后查找必须返回 −1（调用方回退线性扫描），不许静默当成「没有」
    // full-table contract: once the load reaches 3/4 a lookup must return -1 (the caller falls back to a linear scan), never silently "not there"
    { rts_tagset ts; memset(&ts, 0, sizeof ts); rts_tagset_frame(&ts); int rc = 0, added = 0;
      for (int i = 0; i < RTS_TAGSET_CAP && rc >= 0; i++) { uint64_t t = (uint64_t)i * 0x9E3779B97F4A7C15ull + 1; rts_tagset_add(&ts, t); added++; rc = rts_tagset_has(&ts, t + 0x5bf03635ull); }
      printf("满载：加入 %d 项后查找返回 %d（期望 −1）%s\n", added, rc, rc == -1 ? "✓" : "✗"); bad += (rc != -1); }
    int fail = bad || probes_bad;
    printf("== %s（判定 %d、探测超限帧 %d）==\n", fail ? "失败" : "通过", bad, probes_bad);
    return fail != 0;
}
