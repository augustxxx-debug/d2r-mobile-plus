// 安卓性能提示（ADPF：APerformanceHint）与系统温控（AThermal）。
// 天玑 9600 Pro 上实测：主游戏线程 85% CPU 却只跑在 0.7–1.0 GHz（大核最高 4.35 GHz），战斗时每帧提交 60 ms、掉到 10 帧——调度器不知道我们的帧预算。
// 这里每 10 秒按 /proc/self/task/*/schedstat 找本进程最忙的几个线程（主游戏线程、翻译队列等），放进一个性能提示会话，目标时长 = 当前帧率档的帧时长；
// 每帧报关键路径的实际工作量（会话内线程这一帧在 CPU 上跑掉的时间取最大），调度器据此把它们放到大核、按需升频。
// AThermal 的系统温控状态换成与 iOS thermalState 同一套 0–3 档（dm_thermal_now：[perf] 行与 perf 文件的「温控」列），变化时记一行，与 iOS 一样只记日志；
// 3D 渲染比例的降 / 升照旧由实际掉帧驱动（hle_oe.c 动态比例）。
// 运行时 dlsym（APerformanceHint 是 API 33、setThreads 是 API 34，AThermal 是 API 30）：系统没有就跳过。DM_PERFHINT=0 关性能提示（温控照读）。
// Android performance hints (ADPF: APerformanceHint) and system thermal status (AThermal).
// Measured on a Dimensity 9600 Pro: the main game thread ran at 85% CPU but at 0.7–1.0 GHz (big cores reach 4.35 GHz); in combat the per-frame submit took
// 60 ms and the frame rate fell to 10 -- the scheduler knew nothing of our frame budget. Every 10 s the busiest threads of this process (the main game thread,
// the translation queue and so on) are found from /proc/self/task/*/schedstat and put into one hint session whose target is the frame time of the current
// frame-rate mode; every frame reports the critical-path work (the largest CPU time any session thread spent in that frame), so the scheduler places them
// on big cores and raises clocks as needed. AThermal's status maps to iOS thermalState's 0–3 levels (dm_thermal_now: the 「温控」 column of [perf] lines and
// the perf file) and changes are logged; as on iOS it is only logged -- lowering / raising the 3D render scale stays driven by real dropped frames (hle_oe.c).
// Resolved with dlsym at run time (APerformanceHint is API 33, setThreads API 34, AThermal API 30): skipped when absent. DM_PERFHINT=0 disables hints (thermal is still read).
#include <dirent.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "perfhint_android.h"
#include "../host/dm.h"
#include "../host/dm_custom.h"

typedef struct APerformanceHintManager APerformanceHintManager;
typedef struct APerformanceHintSession APerformanceHintSession;
typedef struct AThermalManager AThermalManager;
static struct {
    APerformanceHintManager *(*get_manager)(void);
    APerformanceHintSession *(*create)(APerformanceHintManager *, const int32_t *, size_t, int64_t);
    int (*report)(APerformanceHintSession *, int64_t);
    int (*update_target)(APerformanceHintSession *, int64_t);
    int (*set_threads)(APerformanceHintSession *, const pid_t *, size_t);
    void (*close)(APerformanceHintSession *);
    AThermalManager *(*thermal_acquire)(void);
    int (*thermal_status)(AThermalManager *);
} g;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static APerformanceHintManager *g_mgr; static AThermalManager *g_thermal;
// 以下只由出图线程碰 | Touched only by the present thread from here on
static APerformanceHintSession *g_session; static pid_t g_tids[DM_PERFHINT_MAX_THREADS]; static int g_ntids; static uint64_t g_last_run[DM_PERFHINT_MAX_THREADS];
static int64_t g_target_ns; static uint64_t g_frames, g_next_pick, g_sum_ns, g_n, g_max_ns;

// /proc/self/task/<tid>/schedstat 第一个数 = 该线程累计在 CPU 上的纳秒；读不到 = 0 | The first field = nanoseconds the thread has spent on a CPU; 0 when unreadable
static uint64_t thread_run_ns(pid_t tid) {
    char p[64]; snprintf(p, sizeof p, "/proc/self/task/%d/schedstat", (int)tid);
    FILE *f = fopen(p, "re"); if (!f) return 0; unsigned long long v = 0; if (fscanf(f, "%llu", &v) != 1) v = 0; fclose(f); return v;
}

static void load(void) {
    const char *e = getenv("DM_PERFHINT"); int off = e && !strcmp(e, "0");
    void *lib = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (lib) {
        *(void **)&g.thermal_acquire = dlsym(lib, "AThermal_acquireManager"); *(void **)&g.thermal_status = dlsym(lib, "AThermal_getCurrentThermalStatus");
        if (g.thermal_acquire && g.thermal_status) g_thermal = g.thermal_acquire();
        if (!off) {
            *(void **)&g.get_manager = dlsym(lib, "APerformanceHint_getManager"); *(void **)&g.create = dlsym(lib, "APerformanceHint_createSession");
            *(void **)&g.report = dlsym(lib, "APerformanceHint_reportActualWorkDuration"); *(void **)&g.update_target = dlsym(lib, "APerformanceHint_updateTargetWorkDuration");
            *(void **)&g.set_threads = dlsym(lib, "APerformanceHint_setThreads"); *(void **)&g.close = dlsym(lib, "APerformanceHint_closeSession");
            if (g.get_manager && g.create && g.report) g_mgr = g.get_manager();
        }
    }
    LOG("性能提示（ADPF）：%s；系统温控：%s | performance hints: %s; thermal status: %s", g_mgr ? "可用" : off ? "DM_PERFHINT=0 关" : "系统没有",
        g_thermal ? "可用" : "系统没有", g_mgr ? "on" : off ? "off" : "unavailable", g_thermal ? "available" : "unavailable");
}

int dm_perfhint_pick(const pid_t *tids, const uint64_t *busy_ns, int n, pid_t *out, int max) {
    uint64_t top = 0; for (int i = 0; i < n; i++) if (busy_ns[i] > top) top = busy_ns[i];
    if (!top) return 0;
    int k = 0;
    while (k < max) {   // 已选的不再选（最多 max 个，逐个比对，不用标记数组） | skip the ones already picked (at most max, compared one by one, no marker array)
        int best = -1;
        for (int i = 0; i < n; i++) {
            int taken = 0; for (int j = 0; j < k; j++) if (out[j] == tids[i]) { taken = 1; break; }
            if (!taken && busy_ns[i] * 10 >= top && (best < 0 || busy_ns[i] > busy_ns[best])) best = i;
        }
        if (best < 0) break; out[k++] = tids[best];
    }
    return k;
}

// 重选会话线程（每 10 秒）：两次采样之间的忙碌时长决定谁进会话；线程集变了才换。
// 这里跑在游戏渲染线程自己的栈上（nvnQueuePresentTexture → dm_perf_present），那条调用链本来就深：工作数组全放静态区（只有这一个线程进来），栈上不留大块。
// Re-pick the session threads (every 10 s): busy time between two samples decides; only changes when the set changes.
// This runs on the game's render thread's own stack (nvnQueuePresentTexture -> dm_perf_present), already deep there: the work arrays are static
// (only this one thread comes here), nothing large on the stack.
static void pick_threads(void) {
    enum { N = 512 }; static pid_t prev_tid[N]; static uint64_t prev_run[N]; static int nprev, have_prev; static uint64_t prev_wall;
    static pid_t tid[N]; static uint64_t run[N], busy[N]; int n = 0;
    DIR *d = opendir("/proc/self/task"); if (!d) return; struct dirent *de;
    while ((de = readdir(d)) && n < N) { if (de->d_name[0] < '0' || de->d_name[0] > '9') continue; tid[n] = (pid_t)atoi(de->d_name); run[n] = thread_run_ns(tid[n]); n++; }
    closedir(d);
    uint64_t wall = now_ns();
    for (int i = 0; i < n; i++) { busy[i] = 0; for (int j = 0; j < nprev; j++) if (prev_tid[j] == tid[i]) { busy[i] = run[i] > prev_run[j] ? run[i] - prev_run[j] : 0; break; } }
    int first = !have_prev; uint64_t window = wall - prev_wall;
    memcpy(prev_tid, tid, sizeof(pid_t) * n); memcpy(prev_run, run, sizeof(uint64_t) * n); nprev = n; have_prev = 1; prev_wall = wall;
    if (first || !window) return;   // 第一次只记基线 | the first pass only records a baseline
    pid_t pick[DM_PERFHINT_MAX_THREADS]; int k = dm_perfhint_pick(tid, busy, n, pick, DM_PERFHINT_MAX_THREADS);
    if (!k) return;
    int same = k == g_ntids; for (int i = 0; same && i < k; i++) same = pick[i] == g_tids[i];
    if (same && g_session) return;
    int moved = g_session && g.set_threads && g.set_threads(g_session, pick, (size_t)k) == 0;
    if (!moved) {
        if (g_session && g.close) g.close(g_session);
        int32_t t32[DM_PERFHINT_MAX_THREADS]; for (int i = 0; i < k; i++) t32[i] = pick[i];
        g_session = g.create(g_mgr, t32, (size_t)k, g_target_ns);
    }
    memcpy(g_tids, pick, sizeof(pid_t) * k); g_ntids = k; for (int i = 0; i < k; i++) g_last_run[i] = thread_run_ns(g_tids[i]);
    char list[192]; int at = 0;
    for (int i = 0; i < k; i++) { uint64_t b = 0; for (int j = 0; j < n; j++) if (tid[j] == pick[i]) b = busy[j];
        at += snprintf(list + at, sizeof list - at, "%s%d %.0f%%", i ? "，" : "", (int)pick[i], b * 100.0 / window); if (at >= (int)sizeof list) break; }
    LOG("性能提示：会话%s，线程 %d 个（%s），目标 %.2f ms | hint session %s, %d threads, target %.2f ms", !g_session ? "建不成" : moved ? "换线程" : "新建",
        k, list, g_target_ns / 1e6, !g_session ? "failed" : moved ? "rethreaded" : "created", k, g_target_ns / 1e6);
}

void dm_perfhint_frame(void) { dm_perfhint_frame_at(dm_fps_setting()); }
void dm_perfhint_frame_at(int fps) {
    pthread_once(&g_once, load);
    if (!g_mgr || fps <= 0) return;
    int64_t target = 1000000000ll / fps;
    if (target != g_target_ns) { g_target_ns = target; if (g_session && g.update_target) g.update_target(g_session, target); }
    if (++g_frames >= g_next_pick) { g_next_pick = g_frames + (uint64_t)fps * 10; pick_threads(); }
    if (!g_session) return;
    uint64_t worst = 0;
    for (int i = 0; i < g_ntids; i++) { uint64_t r = thread_run_ns(g_tids[i]), dt = r > g_last_run[i] ? r - g_last_run[i] : 0; g_last_run[i] = r; if (dt > worst) worst = dt; }
    if (!worst) return;
    g.report(g_session, (int64_t)worst);
    g_sum_ns += worst; g_n++; if (worst > g_max_ns) g_max_ns = worst;
    if (g_n >= (uint64_t)fps * 30) {   // 每约 30 秒一行 | one line every ~30 s
        LOG("性能提示：近 %llu 帧关键路径 CPU 平均 %.2f ms、最长 %.2f ms，目标 %.2f ms | hint: critical-path CPU avg %.2f ms, max %.2f ms, target %.2f ms",
            (unsigned long long)g_n, g_sum_ns / 1e6 / g_n, g_max_ns / 1e6, g_target_ns / 1e6, g_sum_ns / 1e6 / g_n, g_max_ns / 1e6, g_target_ns / 1e6);
        g_sum_ns = g_n = g_max_ns = 0;
    }
}

int dm_thermal_android_level(int status) {   // NONE→0；LIGHT / MODERATE→1；SEVERE→2；CRITICAL 及以上→3 | NONE→0; LIGHT / MODERATE→1; SEVERE→2; CRITICAL and above→3
    return status < 0 ? -1 : status == 0 ? 0 : status <= 2 ? 1 : status == 3 ? 2 : 3;
}
int dm_thermal_now(void) {   // 覆盖 hle_nvn.c 的弱定义 | overrides the weak definition in hle_nvn.c
    pthread_once(&g_once, load);
    if (!g_thermal) return -1;
    int raw = g.thermal_status(g_thermal), lv = dm_thermal_android_level(raw);
    static _Atomic int last = -2; int was = atomic_exchange(&last, raw);
    if (was != raw) LOG("温控档位 %d（系统状态 %d）| thermal level %d (system status %d)", lv, raw, lv, raw);
    return lv;
}
