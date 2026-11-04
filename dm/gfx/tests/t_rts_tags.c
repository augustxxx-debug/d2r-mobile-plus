// 本帧实例去重集合的单测（纯 C，不碰 Metal）：怪群场景的绘制次序 —— 同一种怪的多份实例，两级级联各画一次，
// 加上预渲染，一帧 2600 次登记、1070 个不同实例，其中少量绘制会中途退出（网格未就绪 / 槽满），没有登记实例。
// 对照朴素线性扫描（旧生产代码的做法）逐次核对判定完全一致，并钉住「每次登记的工作量有上限」——
// 朴素扫描是 O(登记数 × 实例数)，怪群时每帧上百万次比较。
// 覆盖集合的每条边界：跨帧惰性清理（同一集合连续多帧不 memset）、探测链被强制拉长到超过 32、载入率 3/4 满载、
// 帧代 uint32 回绕、同帧释放后再启用（全零不是合法空表）。
// Unit test of the per-frame instance de-duplication set (pure C, no Metal): the draw pattern of a monster swarm - many units of the
// same type, drawn once per cascade plus the depth prepass: 2600 registrations and 1070 distinct instances per frame, with a few draws
// bailing out half-way (mesh not ready / no slot) without registering an instance. Decisions are checked one by one against the naive
// linear scan (what production did), and the work per registration is pinned: the naive scan is O(draws x instances), i.e. over
// a million comparisons per frame in a swarm. Every edge of the set is covered: lazy cross-frame clearing (one set, no memset between
// frames), a probe chain forced past 32, the 3/4 load limit, uint32 frame-generation wraparound, and release-then-re-enable in the same
// frame (an all-zero table is not a valid empty set).
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

// 生产代码今天的样子：本帧已登记的实例表（只有真登记了的实例在表里）—— g_rts.inst_tag / ninst
// What production has: this frame's table of registered instances (only instances that really registered are in it) - g_rts.inst_tag / ninst
typedef struct { uint64_t tag[INST + 8]; int n; } inst_table;
static int table_has(const inst_table *s, uint64_t tag) { for (int i = 0; i < s->n; i++) if (s->tag[i] == tag) return 1; return 0; }
static void table_add(inst_table *s, uint64_t tag) { if (s->n < INST + 8) s->tag[s->n++] = tag; }
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

// 生产判定（nvn_rtshadow.inc 的 rts_draw_inner 调的就是这一个）：集合说「有」= 本帧已登记；集合不可用（−1，满载 / 探测链过长）时
// 回退扫本帧实例表 —— 与旧的线性扫描逐项相同。下面每一节都经它判定，测的就是生产那一步，不是测试自己另写一份。
// The production decision (the very function rts_draw_inner calls in nvn_rtshadow.inc): "yes" from the set = already registered this frame;
// when the set is unusable (-1: full / probe chain too long) it falls back to a scan of this frame's instance table - item for item what the
// old linear scan did. Every section below decides through it, so what is tested is the production step, not a copy of it.
static int decide(rts_tagset *ts, const inst_table *it, uint64_t tag) { return rts_tagset_dup(ts, tag, it->tag, it->n); }

#define MAXD 2700
static uint64_t stream[FRAMES][MAXD]; static int drop[FRAMES][MAXD], ndrawn[FRAMES];

// 一帧的登记序列：先 1070 个新实例，再让其中约一半被第二级级联 / 预渲染重复登记一次（次序打乱），
// 夹少量每帧新出现的实例（怪群逐批刷怪）；另有约 3% 的绘制中途退出、没有登记实例
// one frame's registrations: 1070 new instances, then about half of them drawn a second time by the other cascade / the prepass in a
// shuffled order, plus a few instances new this frame; about 3% of the draws bail out without registering an instance
static void make_stream(void) {
    for (int f = 0; f < FRAMES; f++) {
        static uint64_t tags[INST]; int nd = 0;
        for (int i = 0; i < INST; i++) tags[i] = (uint64_t)(i < INST - 8 ? i : rnd()) * 0x9E3779B97F4A7C15ull ^ (0x1234 + f);
        for (int i = 0; i < INST; i++) { stream[f][nd] = tags[i]; drop[f][nd] = (rnd() % 100) < 3; nd++; }
        for (int i = 0; i < (INST * 3) / 2 && nd < DRAWS; i++) { int k = (int)(rnd() % INST); stream[f][nd] = tags[k]; drop[f][nd] = 0; nd++; }
        for (; nd < DRAWS; nd++) { stream[f][nd] = tags[rnd() % INST]; drop[f][nd] = (rnd() % 100) < 3; }
        ndrawn[f] = nd;
    }
}

int main(void) {
    make_stream();
    int bad = 0;
    // 1. 跨帧惰性清理：同一个集合连续 120 帧，只在帧头 rts_tagset_frame（不清内存），判定必须与朴素扫描逐次相同；
    //    上一帧留下的标签不许在本帧当成「已有」
    // cross-frame lazy clearing: one set for 120 frames, only rts_tagset_frame at the frame start (no memset); decisions must match the
    // naive scan registration by registration, and a tag left over from the previous frame must not count as "already this frame"
    { rts_tagset ts; memset(&ts, 0, sizeof ts); rts_tagset_frame(&ts);
      int leak = 0; uint64_t max_probe = 0, probes_bad = 0;
      for (int f = 0; f < FRAMES; f++) {
        inst_table ref = {0};   // 本帧实例表：帧头清空，只放真登记了的实例 | this frame's table: cleared at the frame start, only registered instances
        uint64_t prev0 = ts.probes;
        rts_tagset_frame(&ts);
        if (f > 0 && ts.n != 0) leak++;   // 帧头必须把本帧计数归零（内存不清）| the frame head must reset the count (memory is not cleared)
        for (int i = 0; i < ndrawn[f]; i++) {
            uint64_t tag = stream[f][i];
            int want = table_has(&ref, tag) ? 1 : 0;
            int got = decide(&ts, &ref, tag);
            if (got != want) { if (!bad) printf("第 %d 帧第 %d 次登记：期望 %d，实得 %d\n", f, i, want, got); bad++; }
            if (!want && !drop[f][i]) { table_add(&ref, tag); rts_tagset_add(&ts, tag); }
        }
        if (ts.probes - prev0 > max_probe) max_probe = ts.probes - prev0;
        if (ts.probes - prev0 > (uint64_t)ndrawn[f] * PROBE_BUDGET) probes_bad++;
      }
      printf("跨帧惰性清理 + 判定一致：%s（不一致 %d 次，帧头没归零 %d 次）\n", (bad || leak) ? "否 ✗" : "是 ✓", bad, leak);
      printf("每帧探测量峰值 %llu（上限 %d，超出帧数 %llu）\n", (unsigned long long)max_probe, DRAWS * PROBE_BUDGET, (unsigned long long)probes_bad);
      bad += leak + (probes_bad != 0); }
    // 2. 中途退出这一条单独再核一遍：同一标签先退出、后正常绘制，第二次必须是「新登记」
    // the bail-out rule on its own: the same tag bails out first, then draws normally - the second time must count as new
    { rts_tagset ts; memset(&ts, 0, sizeof ts); rts_tagset_frame(&ts); inst_table it = {0};
      uint64_t t = 0xabcdefull; int a = decide(&ts, &it, t), c = decide(&ts, &it, t), d = decide(&ts, &it, t);
      table_add(&it, t); rts_tagset_add(&ts, t); int e = decide(&ts, &it, t);
      printf("先退出后登记：%d %d %d → 登记后 %d（期望 0 0 0 → 1）%s\n", a, c, d, e, (a || c || d || !e) ? "✗" : "✓");
      bad += (a || c || d || !e); }
    // 3. 强制长探测链（> 32）：挑 40 个起始槽相同的标签依次登记，集合只收得下 32 个（探测上限），后 8 个的查找必然走满 32 次 ——
    //    必须返回 −1 让调用方回退，回退之后「已登记 = 1、没登记 = 0」；不许当成「没有」（那是丢影），也不许把未登记的当成已有
    // a forced long probe chain (> 32): 40 tags sharing one start slot registered in order; the set can keep only 32 of them (probe limit),
    // so the last 8 lookups must run the full 32 probes - they have to return -1 and let the caller fall back, after which
    // "registered = 1, not registered = 0"; never "not there" (that loses a shadow) and never a false duplicate
    { uint64_t bunch[40]; int found = 0; uint32_t slot0 = 0;
      for (uint64_t x = 0x1000; found < 40 && x < 40000000ull; x++) {
        uint32_t i = (uint32_t)((x * 0x9E3779B97F4A7C15ull) >> 40) & (RTS_TAGSET_CAP - 1);
        if (!found) { slot0 = i; bunch[found++] = x; }
        else if (i == slot0) bunch[found++] = x;
      }
      rts_tagset ts; memset(&ts, 0, sizeof ts); rts_tagset_frame(&ts); inst_table it = {0};
      int hits = 0, miss_bad = 0, regs = 0, stored = 0;
      printf("长链：%d 个标签共用起始槽 %u\n", found, (unsigned)slot0);
      for (int i = 0; i < found; i++) {
        int want = table_has(&it, bunch[i]) ? 1 : 0;
        int got = decide(&ts, &it, bunch[i]);
        if (got != want) { miss_bad++; if (miss_bad <= 3) printf("  长链第 %d 个：期望 %d，实得 %d\n", i, want, got); }
        if (!want) { uint32_t n0 = ts.n; table_add(&it, bunch[i]); rts_tagset_add(&ts, bunch[i]); regs++; stored += ts.n != n0; }
        if (rts_tagset_has(&ts, bunch[i]) < 0) hits++;   // 原始查找真的走不动了 | the raw lookup really ran out of probes
      }
      printf("长链：登记 %d 项、集合只收下 %d 项（探测上限）、原始查找返回 −1（必须回退）的 %d 次，判定不一致 %d 次 %s\n",
             regs, stored, hits, miss_bad, (regs == found && stored < regs && hits > 0 && miss_bad == 0) ? "✓" : "✗");
      bad += (found < 40) || (stored >= regs) || (hits == 0) || (miss_bad != 0); }
    // 4. 满载（载入率 3/4）：查找一律返回 −1，回退后「已登记 = 1、不存在 = 0」
    // full table (load 3/4): lookups all return -1, and after the fallback "registered = 1, absent = 0"
    { rts_tagset ts; memset(&ts, 0, sizeof ts); rts_tagset_frame(&ts); inst_table it = {0};
      int rc = 0, added = 0;
      for (int i = 0; i < RTS_TAGSET_CAP && rc >= 0; i++) { uint64_t t = (uint64_t)i * 0x9E3779B97F4A7C15ull + 1; table_add(&it, t); rts_tagset_add(&ts, t); added++; rc = rts_tagset_has(&ts, t + 0x5bf03635ull); }
      int wrong = 0, raw_m1 = 0;
      for (int i = 0; i < 512; i++) {   // 已登记 512 个：必须判 1 | 512 registered: must decide 1
        if (rts_tagset_has(&ts, (uint64_t)i * 0x9E3779B97F4A7C15ull + 1) < 0) raw_m1++;
        int g = decide(&ts, &it, (uint64_t)i * 0x9E3779B97F4A7C15ull + 1); if (g != 1) wrong++;
        uint64_t miss = 0xdead0000ull + (uint64_t)i * 0x9E3779B97F4A7C15ull;   // 没登记：必须判 0 | not registered: must decide 0
        if (table_has(&it, miss)) { wrong++; continue; }
        if (decide(&ts, &it, miss) != 0) wrong++;
      }
      printf("满载：加入 %d 项后查找返回 %d（期望 −1），原始查找返回 −1 的 %d 次，已登记 / 不存在判错 %d 次 %s\n",
             added, rc, raw_m1, wrong, (rc == -1 && wrong == 0 && raw_m1 > 0) ? "✓" : "✗");
      bad += (rc != -1) || wrong; }
    // 5. 帧代 uint32 回绕：cur = UINT32_MAX 之后进新帧，cur 回到 1、gen 全 0、上一帧的标签不再命中
    // uint32 frame-generation wraparound: after cur = UINT32_MAX a new frame gives cur = 1, all gen = 0, and last frame's tags no longer hit
    { rts_tagset ts; memset(&ts, 0, sizeof ts); rts_tagset_frame(&ts); inst_table it = {0};
      for (int i = 0; i < 64; i++) { uint64_t t = 0x7000000ull + (uint64_t)i * 0x9E3779B97F4A7C15ull; table_add(&it, t); rts_tagset_add(&ts, t); }
      uint32_t before = ts.cur; ts.cur = 0xffffffffu;
      rts_tagset_frame(&ts); it.n = 0;   // 新帧：实例表也清空 | a new frame: the instance table is empty too
      int stale = 0, gen_left = 0;
      for (int i = 0; i < 64; i++) { uint64_t t = 0x7000000ull + (uint64_t)i * 0x9E3779B97F4A7C15ull; if (decide(&ts, &it, t) != 0) stale++; }
      for (int i = 0; i < RTS_TAGSET_CAP; i++) gen_left += ts.gen[i] != 0;
      printf("帧代回绕：cur %u → UINT32_MAX → %u（期望 1），残留 gen %d 个，旧标签仍命中 %d 次，n=%u %s\n",
             (unsigned)before, (unsigned)ts.cur, gen_left, stale, ts.n,
             (ts.cur == 1 && gen_left == 0 && stale == 0 && ts.n == 0) ? "✓" : "✗");
      bad += !(ts.cur == 1 && gen_left == 0 && stale == 0 && ts.n == 0); }
    // 6. 同帧释放后再启用：rts_release_all 之后集合必须是合法空表。旧写法 memset 整个结构体 → cur == 0 而 gen 全 0，
    //    「本帧已有」和空位分不开：标签 0 被误判重复（丢影），非零标签每次探测 32 次后回退（白扫）。
    //    新写法 reset：gen 清零、cur 置 1、n 归零 —— 空表不变式成立，同一帧号直接再画也不出错。
    // release then re-enable in the same frame: after rts_release_all the set must be a valid empty table. The old memset left cur == 0
    // with gen all 0, so "already this frame" and "free slot" were indistinguishable: tag 0 counted as a duplicate (lost shadow) and any
    // other tag burned 32 probes then fell back. The new reset clears gen, sets cur = 1, n = 0 - the empty-table invariant holds, and
    // drawing again in the same frame number is correct.
    { rts_tagset ts; memset(&ts, 0, sizeof ts);
      int z0 = rts_tagset_has(&ts, 0), z1 = rts_tagset_has(&ts, 1);
      rts_tagset_reset(&ts);   // 生产 rts_release_all 的写法 | what production rts_release_all does now
      inst_table it = {0};
      int a0 = rts_tagset_has(&ts, 0), a1 = rts_tagset_has(&ts, 1), a2 = decide(&ts, &it, 0);
      table_add(&it, 0); rts_tagset_add(&ts, 0);
      int a3 = decide(&ts, &it, 0);   // 登记之后才算重复 | only after registering does it count as a duplicate
      printf("释放后再启用：全零（旧写法）has(0)=%d has(1)=%d；reset 后 has(0)=%d has(1)=%d、判定 %d → 登记后 %d（期望 0、0、0 0 → 0 → 1）%s\n",
             z0, z1, a0, a1, a2, a3, (a0 || a1 || a2 || !a3) ? "✗" : "✓");
      bad += (a0 || a1 || a2 || !a3); }
    // 7. 主机微基准（不是生产收益）：同一段登记序列、15 轮交错、取中位数与范围。只作算法级对照 —— 本机 CPU、
    //    合成序列、不含 GPU 与真机帧时间，不能当帧率 / 功耗数字用
    // host micro-benchmark (not a production gain): the same registration stream, 15 interleaved rounds, median and range. Algorithm-level
    // reference only - this CPU, a synthetic stream, no GPU and no device frame times; it is not a frame-rate or power figure.
    { enum { ROUNDS = 15 }; double tn[ROUNDS], ts_ms[ROUNDS]; volatile uint64_t sink = 0;
      for (int warm = 0; warm < 3; warm++) {   // 预热 | warm-up
        inst_table it = {0}; for (int i = 0; i < ndrawn[0]; i++) { if (table_has(&it, stream[0][i])) continue; if (!drop[0][i]) table_add(&it, stream[0][i]); } sink += (uint64_t)it.n;
        rts_tagset t; memset(&t, 0, sizeof t); rts_tagset_frame(&t); for (int i = 0; i < ndrawn[0]; i++) { if (rts_tagset_has(&t, stream[0][i]) > 0) continue; if (!drop[0][i]) rts_tagset_add(&t, stream[0][i]); } sink += t.n;
      }
      for (int r = 0; r < ROUNDS; r++) {
        double t0 = now_ms();
        for (int f = 0; f < FRAMES; f++) { inst_table it = {0}; for (int i = 0; i < ndrawn[f]; i++) { if (table_has(&it, stream[f][i])) continue; if (!drop[f][i]) table_add(&it, stream[f][i]); } sink += (uint64_t)it.n; }
        tn[r] = (now_ms() - t0) / FRAMES;
        t0 = now_ms();
        for (int f = 0; f < FRAMES; f++) { rts_tagset t; memset(&t, 0, sizeof t); rts_tagset_frame(&t); for (int i = 0; i < ndrawn[f]; i++) { if (rts_tagset_has(&t, stream[f][i]) > 0) continue; if (!drop[f][i]) rts_tagset_add(&t, stream[f][i]); } sink += t.n; }
        ts_ms[r] = (now_ms() - t0) / FRAMES;
      }
#define MED(a) ({ double c_[ROUNDS]; memcpy(c_, a, sizeof c_); for (int i_ = 1; i_ < ROUNDS; i_++) for (int j_ = i_; j_ > 0 && c_[j_] < c_[j_ - 1]; j_--){ double x_ = c_[j_]; c_[j_] = c_[j_ - 1]; c_[j_ - 1] = x_; } c_[ROUNDS / 2]; })
      double mn = MED(tn), ms = MED(ts_ms), nmin = tn[0], nmax = tn[0], smin = ts_ms[0], smax = ts_ms[0];
      for (int r = 1; r < ROUNDS; r++) { if (tn[r] < nmin) nmin = tn[r]; if (tn[r] > nmax) nmax = tn[r]; if (ts_ms[r] < smin) smin = ts_ms[r]; if (ts_ms[r] > smax) smax = ts_ms[r]; }
      printf("主机微基准（%d 轮交错，%d 帧/轮，本机 CPU 合成序列，非真机帧时间）：朴素扫描中位 %.3f ms/帧（%.3f–%.3f），去重集合中位 %.3f ms/帧（%.3f–%.4f），中位比 %.2fx（汇点 %llu）\n",
             ROUNDS, FRAMES, mn, nmin, nmax, ms, smin, smax, ms > 0 ? mn / ms : 0, (unsigned long long)sink);
#undef MED
    }
    printf("== %s（不一致 / 越界 %d 次）==\n", bad ? "失败" : "通过", bad);
    return bad != 0;
}
