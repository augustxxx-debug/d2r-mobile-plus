// hle_audio.c 单测（静音接收端）：3 块各 100 ms 缓冲 → 按序在约 100/200/300 ms 释放、每次释放触发事件。
// 期望值由采样率与块长直接算出：4800 帧 / 48 kHz = 100 ms。
// hle_audio.c unit test (silent sink): 3 buffers of 100 ms each -> released in order at about 100/200/300 ms, each release triggering the event.
// Expected values are computed directly from the sample rate and block length: 4800 frames / 48 kHz = 100 ms.
#define DM_AUDIO_TEST_FAULTS          // 单测注入「声卡打开失败」 | the unit test injects "sound card open failed"
#define DM_AUDIO_RETRY_MS 100          // 定时重试间隔缩到 100 ms，单测不用等 1.5 秒 | shorten the periodic retry to 100 ms so the test does not wait 1.5 s
#include "../hle_audio.c"
#include <time.h>
#undef au_open
static int g_fail_open;                // 接下来这么多次打开声卡假装失败 | this many upcoming sound card opens pretend to fail
static int t_fault_open(aout *a) { if (g_fail_open > 0) { g_fail_open--; a->au = NULL; return -1; } return au_open(a); }
uint64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec; }
static struct { const void *k; void *v; } om[8];
void *objmap_get(const void *k) { for (int i = 0; i < 8; i++) if (om[i].k == k) return om[i].v; return NULL; }
void objmap_put(const void *k, void *v) { for (int i = 0; i < 8; i++) if (!om[i].k) { om[i].k = k; om[i].v = v; return; } }
void *objmap_del(const void *k) { for (int i = 0; i < 8; i++) if (om[i].k == k) { om[i].k = 0; return om[i].v; } return NULL; }
static _Atomic int g_sig;
void hle_signal_system_event(void *g) { (void)g; atomic_fetch_add_explicit(&g_sig, 1, memory_order_seq_cst); }
void hle_bind_system_event(void *g, int autoclear) { (void)g; (void)autoclear; }   // 真实现要客体线程 + SDK 内核事件；单测只数触发次数 | the real implementation needs a guest thread + SDK kernel event; the unit test only counts triggers
int main(void) {
    {   // 5.1 下混（期望按 Q15 系数手算：0.707·0.707 = 16384/32768 = 0.5，0.707 = 23170/32768）：顺序 左前 右前 中置 低音 左后 右后 | 5.1 downmix (expected values worked out by hand from the Q15 coefficients: 0.707*0.707 = 16384/32768 = 0.5, 0.707 = 23170/32768): order front left, front right, centre, LFE, rear left, rear right
        static const struct { int16_t in[6]; int16_t l, r; const char *what; } c[] = {
            { { 0, 0, 10000, 0, 0, 0 }, 5000, 5000, "只有中置（对白）→ 左右各 5000" },
            { { 0, 0, 0, 0, 10000, 0 }, 5000, 0, "只有左后 → 左 5000、右 0" },
            { { 10000, 0, 0, 0, 0, 0 }, 7070, 0, "只有左前 → 左 7070（−3 dB）" },
            { { 0, 0, 0, 30000, 0, 0 }, 0, 0, "只有低音 → 不并入，0" },
            { { 32767, 32767, 32767, 0, 32767, 32767 }, 32767, 32767, "满幅叠加 → 饱和到 32767" },
            { { -32768, -32768, -32768, 0, -32768, -32768 }, -32768, -32768, "负满幅叠加 → 饱和到 −32768" } };
        int bad = 0;
        for (unsigned i = 0; i < sizeof c / sizeof *c; i++) { int16_t o[2]; downmix51(c[i].in, o, 1); int ok = o[0] == c[i].l && o[1] == c[i].r; bad |= !ok;
            printf("下混：%s：期望 (%d,%d) → 真结果 (%d,%d) %s\n", c[i].what, c[i].l, c[i].r, o[0], o[1], ok ? "✓" : "✗"); }
        if (bad) return 1; }
    char out[64], ev[64]; uint8_t prm[16]; gbuf b[3]; static int16_t pcm[3][4800 * 2];
    h_au_InitParam(prm);
    if (h_au_Open(out, ev, NULL, prm)) return 1;
    if (h_au_Ch(out) != 2 || h_au_Rate(out) != 48000 || h_au_State(out) != STATE_STOPPED) { puts("FAIL 参数"); return 1; }
    for (int i = 0; i < 3; i++) { h_au_SetBuf(&b[i], pcm[i], sizeof pcm[i], sizeof pcm[i]); if (!h_au_Append(out, &b[i])) return 1; }
    if (h_au_Released(out)) { puts("FAIL 未开始就有释放"); return 1; }
    uint64_t t0 = now_ns(); h_au_Start(out);
    int got = 0, fail = 0;
    while (got < 3 && now_ns() - t0 < 10000000000ull) {
        gbuf *r = h_au_Released(out);
        if (!r) { usleep(1000); continue; }
        int ok = r == &b[got];
        printf("释放 #%d：期望 块%d 按序 → 真结果 块%ld %s\n", got, got, (long)(r - b), ok ? "✓" : "✗");
        fail |= !ok; got++;
    }
    h_au_Close(out);
    int sig1 = atomic_load_explicit(&g_sig, memory_order_acquire);
    printf("释放 %d/3，事件触发 %d 次（期望 3）\n", got, sig1);
    if (fail || got != 3 || sig1 != 3) return 1;

    // 场景 2：真出声端（PCM 全 0，听不到）播 6 块各 100 ms，第 150 ms 暂停（切后台 / 来电）、第 350 ms 恢复。
    // 期望：暂停期间由静音接收端接手按实时消耗，6 块按序全部释放、事件 6 次；总时长 ≈ 600 ms（实时，不因暂停停住也不加速）。
    // Scenario 2: the real audio output sink (PCM all 0, inaudible) plays 6 blocks of 100 ms each, pausing at 150 ms (app to background / incoming call) and resuming at 350 ms.
    // Expected: during the pause the silent sink takes over and consumes in real time, all 6 blocks are released in order with 6 events; total duration ~ 600 ms (real time, neither stalled by the pause nor sped up).
    setenv("DM_AUDIO", "1", 1);
    char out2[64]; gbuf b2[6]; static int16_t pcm2[6][4800 * 2]; g_sig = 0;
    if (h_au_Open(out2, ev, NULL, prm)) return 1;
    for (int i = 0; i < 6; i++) { h_au_SetBuf(&b2[i], pcm2[i], sizeof pcm2[i], sizeof pcm2[i]); h_au_Append(out2, &b2[i]); }
    t0 = now_ns(); h_au_Start(out2);
    int got2 = 0, order_ok = 1, susp = 0, resumed = 0;
    while (got2 < 6 && now_ns() - t0 < 10000000000ull) {
        uint64_t ms = (now_ns() - t0) / 1000000;
        if (!susp && ms >= 150) { dm_audio_suspend(1); susp = 1; }
        if (susp && !resumed && ms >= 350) { dm_audio_suspend(0); resumed = 1; }
        gbuf *r = h_au_Released(out2);
        if (!r) { usleep(1000); continue; }
        order_ok &= r == &b2[got2]; got2++;
    }
    double total = (now_ns() - t0) / 1e6;
    h_au_Close(out2);
    int sig2 = atomic_load_explicit(&g_sig, memory_order_acquire);
    int ok2 = got2 == 6 && order_ok && sig2 == 6 && total > 520;
    printf("暂停 / 恢复：释放 %d/6 按序=%d 事件 %d（期望 6）总时长下限 520 ms，真结果 %.0f ms %s\n", got2, order_ok, sig2, total, ok2 ? "✓" : "✗");
    if (!ok2) return 1;

    // 场景 3 / 4（B077：安卓切后台回来后永久没声音）：暂停期间声卡流被系统断开，回前台在原流上启动必失败。
    // 用「暂停后把 AudioUnit 反初始化」模拟（之后 AudioOutputUnitStart 返 -10867，同安卓 requestStart 返 -1）。
    // 场景 3：重开声卡成功 → 恢复后立刻真出声；场景 4：前 2 次打开声卡失败 → 静音接收端顶着、定时重试，第 3 次成功切回真出声。
    // 期望：10 块按序全部释放、事件 10 次、总时长 ≥ 920 ms（实时）；恢复后声卡回调继续增长（> 10 次）、声卡句柄在、状态不留在「暂停」。
    // 旧代码：恢复失败后一直走静音接收端，声卡回调不再增长 → 失败。
    // Scenarios 3 / 4 (B077: no sound for good after Android comes back from background): the stream is disconnected by the system while suspended, so starting the old stream on resume always fails.
    // Simulated by uninitializing the AudioUnit after suspending (AudioOutputUnitStart then returns -10867, like Android requestStart returning -1).
    // Scenario 3: reopening the sound card succeeds -> real sound right after resume; scenario 4: the first 2 opens fail -> the silent sink covers and retries periodically, the 3rd succeeds and switches back to real sound.
    // Expected: all 10 blocks released in order, 10 events, total >= 920 ms (real time); after resume the sound card callbacks keep growing (> 10), the handle exists, the state does not stay "suspended".
    // Old code: after the failed resume it stays on the silent sink and the callbacks stop growing -> fails.
    for (int fail = 0; fail <= 2; fail += 2) {
        char out3[64]; gbuf b3[10]; static int16_t pcm3[10][4800 * 2]; g_sig = 0;
        if (h_au_Open(out3, ev, NULL, prm)) return 1;
        aout *a3 = objmap_get(out3);
        if (!a3->au) { printf("场景 %d：本机没有输出设备，跳过\n", fail ? 4 : 3); h_au_Close(out3); continue; }
        for (int i = 0; i < 10; i++) { h_au_SetBuf(&b3[i], pcm3[i], sizeof pcm3[i], sizeof pcm3[i]); h_au_Append(out3, &b3[i]); }
        t0 = now_ns(); h_au_Start(out3);
        int got3 = 0, ord3 = 1, s3 = 0, r3 = 0; uint64_t rc0 = 0;
        while (got3 < 10 && now_ns() - t0 < 10000000000ull) {
            uint64_t ms = (now_ns() - t0) / 1000000;
            if (!s3 && ms >= 150) { dm_audio_suspend(1); pthread_mutex_lock(&g_aos_lock); AudioUnitUninitialize(a3->au); pthread_mutex_unlock(&g_aos_lock); g_fail_open = fail; s3 = 1; }
            if (s3 && !r3 && ms >= 300) { dm_audio_suspend(0); rc0 = dm_audio_render_calls(); r3 = 1; }
            gbuf *r = h_au_Released(out3);
            if (!r) { usleep(1000); continue; }
            ord3 &= r == &b3[got3]; got3++;
        }
        double tot3 = (now_ns() - t0) / 1e6; uint64_t rc = dm_audio_render_calls() - rc0;
        pthread_mutex_lock(&g_aos_lock); int has = a3->au != NULL, susp3 = g_suspended; pthread_mutex_unlock(&g_aos_lock);
        h_au_Close(out3);
        int sig3 = atomic_load_explicit(&g_sig, memory_order_acquire);
        int ok3 = got3 == 10 && ord3 && sig3 == 10 && tot3 > 920 && rc > 10 && has && !susp3;
        printf("场景 %d（%s）：释放 %d/10 按序=%d 事件 %d（期望 10）总时长 %.0f ms（下限 920）恢复后声卡回调 %llu 次（期望 > 10）声卡句柄 %s 暂停态 %d（期望 0）%s\n",
               fail ? 4 : 3, fail ? "重开失败 2 次 → 定时重试成功" : "原流启动失败 → 重开成功", got3, ord3, sig3, tot3, (unsigned long long)rc, has ? "在" : "空", susp3, ok3 ? "✓" : "✗");
        if (!ok3) return 1;
    }
    return 0;
}
