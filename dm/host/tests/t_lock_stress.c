// 锁交接压力单测（RC1 阻塞项：局内偶发卡死「卡在 MonsterHealth:ClearInfo」）。
// 现场（Mac 71b14f0 a-71-rt1、Duo 6c8d67e D/E_hint*_120）：锁字已交给等待者（0x40001001），等待者状态「运行 / 未在等」（woke=1、wkind=0），
// 却还停在 __psynch_cvwait（自己的条件变量、宿主全局锁 G）——宿主侧逻辑都在 G 下，是系统那一层把这次条件变量信号丢了，等待者永远等不到。
// 多个宿主线程按 SDK 用户态写法（nn::os InternalCriticalSection：CAS 抢锁、置等待位后 svc 0x1A、放锁 CAS 不成就 svc 0x1B）抢同一把客体锁：
//   第 1 段：自然争抢 LOCK_STRESS_SEC 秒（缺省 3），要求互斥不破、全程不停顿；
//   第 2 段：用钩子 dm_test_drop_wakes 让接下来 3 次唤醒「信号丢失」（系统丢信号的确定性复现），要求 1.5 秒内照样有进展（等待按时间片自查 woke，丢了也能自己醒）。
// 两份构建：t_lock_stress（现行：必须全过）与 t_lock_stress_old（-DDM_TEST_NO_WAKE_SLICE 去掉时间片自查，第 2 段必须判出卡死 = 负对照有效）。
// 第 2 段另起一个线程每 1 ms 不持锁读自愈计数 dm_hle_lost_wake_recoveries()（十五审 Q15-I01：计数原先是普通变量、持 G 加、不持 G 原子读 = 数据竞争）；
// t_lock_stress_tsan 用 TSan 构建、报竞争即失败（halt_on_error=1）。
// Lock handoff stress unit test (RC1 blocker: occasional in-run hang "stuck in MonsterHealth:ClearInfo").
// Evidence (Mac 71b14f0 a-71-rt1, Duo 6c8d67e D/E_hint*_120): the lock word was handed to the waiter (0x40001001) and the waiter reads "running / not waiting" (woke=1, wkind=0),
// yet it still sits in __psynch_cvwait (its own condition variable, host global lock G) - all host-side logic runs under G, so the OS layer lost that condition-variable signal and the waiter never wakes.
// Several host threads take one guest lock the SDK's user-mode way (nn::os InternalCriticalSection: CAS to take, set the waiters bit then svc 0x1A, release by CAS or else svc 0x1B):
//   part 1: natural contention for LOCK_STRESS_SEC seconds (default 3), mutual exclusion must hold and progress must never stall;
//   part 2: the hook dm_test_drop_wakes makes the next 3 wake-ups "lose their signal" (a deterministic stand-in for the OS dropping it); progress must resume within 1.5 s (waits re-check woke every time slice, so a lost signal still wakes).
// Two builds: t_lock_stress (current: must fully pass) and t_lock_stress_old (-DDM_TEST_NO_WAKE_SLICE removes the time-slice re-check; part 2 must detect the hang = the negative control is valid).
// In part 2 a separate thread reads the recovery counter dm_hle_lost_wake_recoveries() every 1 ms without the lock (review 15 Q15-I01: the counter used to be a plain variable incremented under G and read atomically without G = a data race);
// t_lock_stress_tsan builds this under TSan and fails on any race report (halt_on_error=1).
#include "../dm.h"
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
int svc_thread(int num, uint64_t *regs);
uint32_t svc_thread_main_init(void);
uint32_t dm_test_thread_attach(int prio);
void (*dm_test_cv_after_release)(void);
extern int dm_test_drop_wakes;   // hle_thread.c（DM_TEST_HOOKS），用 __atomic 读写 | hle_thread.c (DM_TEST_HOOKS), read and written with __atomic
__attribute__((weak)) uint64_t dm_hle_lost_wake_recoveries(void);

#define WAITERS 0x40000000u
#define NW 8
static _Atomic uint32_t g_lock;          // 客体锁字 | guest lock word
static _Atomic int g_in_cs, g_violations, g_stop;
static _Atomic uint64_t g_progress;
static void guest_lock(uint32_t tag) {
    for (;;) {
        uint32_t v = 0;
        if (atomic_compare_exchange_strong(&g_lock, &v, tag)) return;
        if (!(v & WAITERS)) { if (!atomic_compare_exchange_strong(&g_lock, &v, v | WAITERS)) continue; v |= WAITERS; }
        uint64_t r[8] = { v & ~WAITERS, (uint64_t)&g_lock, tag };
        svc_thread(0x1A, r);
        if ((atomic_load(&g_lock) & ~WAITERS) == tag) return;   // 内核已把锁交给本线程 | the kernel handed the lock to this thread
    }
}
static void guest_unlock(uint32_t tag) {
    uint32_t v = tag;
    if (atomic_compare_exchange_strong(&g_lock, &v, 0)) return;
    uint64_t r[8] = { (uint64_t)&g_lock };
    svc_thread(0x1B, r);
}
static void *worker(void *a) {
    uint32_t tag = dm_test_thread_attach((int)(intptr_t)a);
    while (!atomic_load(&g_stop)) {
        guest_lock(tag);
        int z = 0; if (!atomic_compare_exchange_strong(&g_in_cs, &z, 1)) atomic_fetch_add(&g_violations, 1);
        for (volatile int k = 0; k < 50; k++) {}
        atomic_store(&g_in_cs, 0);
        guest_unlock(tag);
        atomic_fetch_add(&g_progress, 1);
    }
    return NULL;
}
static void *poller(void *a) {   // 不持锁轮询自愈计数（诊断接口的真实用法：别的线程随时读）| poll the recovery counter without the lock (the diagnostic interface's real use: any thread reads it at any time)
    (void)a; uint64_t last = 0;
    while (!atomic_load(&g_stop)) { uint64_t n = dm_hle_lost_wake_recoveries ? dm_hle_lost_wake_recoveries() : 0; if (n < last) atomic_fetch_add(&g_violations, 1); last = n; usleep(1000); }
    return NULL;
}
// 观察 sec 秒：任何一段 stall_ms 毫秒没有进展就算卡死，返回 0 | watch for sec seconds: any stretch of stall_ms without progress is a hang, returns 0
static int watch(double sec, int stall_ms, uint64_t *done) {
    uint64_t last = atomic_load(&g_progress); int idle = 0;
    for (int ms = 0; ms < (int)(sec * 1000); ms += 10) {
        usleep(10000); uint64_t p = atomic_load(&g_progress);
        if (p != last) { last = p; idle = 0; } else if ((idle += 10) >= stall_ms) { *done = p; return 0; }
    }
    *done = last; return 1;
}
int main(void) {
    svc_thread_main_init();
    const char *e = getenv("LOCK_STRESS_SEC"); double sec = e ? atof(e) : 3;
    pthread_t th[NW];
    for (int i = 0; i < NW; i++) pthread_create(&th[i], NULL, worker, (void *)(intptr_t)(i & 1 ? 28 : 44));
    uint64_t n1 = 0, n2 = 0;
    int ok1 = watch(sec, 1500, &n1) && atomic_load(&g_violations) == 0;
    printf("%s 第 1 段 自然争抢 %.0f 秒：%d 线程共 %llu 次加解锁，互斥破坏 %d 次，%s\n", ok1 ? "✓" : "✗", sec, NW, (unsigned long long)n1, atomic_load(&g_violations), ok1 ? "全程有进展" : "出现 1.5 秒无进展");
    pthread_t pl; pthread_create(&pl, NULL, poller, NULL);
    __atomic_store_n(&dm_test_drop_wakes, 3, __ATOMIC_RELEASE);
    int ok2 = watch(2, 1500, &n2) && __atomic_load_n(&dm_test_drop_wakes, __ATOMIC_ACQUIRE) == 0;
    uint64_t rec = dm_hle_lost_wake_recoveries ? dm_hle_lost_wake_recoveries() : 0;
#ifdef DM_TEST_NO_WAKE_SLICE
    int ok = ok1 && !ok2;
    printf("%s 负对照（无时间片自查）第 2 段：丢 3 次信号后%s（期望卡死）\n", ok ? "✓" : "✗", ok2 ? "仍有进展" : "1.5 秒无进展 = 卡死");
#else
    int ok = ok1 && ok2 && rec >= 1;
    printf("%s 第 2 段 丢 3 次信号：%s，未消耗的丢弃 %d，自愈 %llu 次（期望 ≥ 1）\n", ok2 && rec >= 1 ? "✓" : "✗", ok2 ? "照样有进展" : "1.5 秒无进展 = 卡死",
           __atomic_load_n(&dm_test_drop_wakes, __ATOMIC_ACQUIRE), (unsigned long long)rec);
#endif
    printf(ok ? "全过\n" : "失败\n");
    fflush(stdout); _exit(ok ? 0 : 1);   // 卡死的线程不收拾，直接退出 | stuck threads are not cleaned up, just exit
}
