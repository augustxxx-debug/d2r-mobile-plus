// 崩溃围栏，接口见 dm_crashfence.h。
// 什么算「正常结束」：宿主优雅退出（dm_host_shutdown：iOS 场景断开 / App 终止、Mac 关窗、SIGTERM）和 iOS 进后台。进后台也清，是因为 iOS 在后台杀进程（玩家在多任务界面划掉、系统收内存）
// 是直接 SIGKILL、不走任何回调，若不清会把这类正常结束当成闪退；回前台再写回。前台里被看门狗杀、闪退、断电都留下标记。
// 连续次数：标记里记本局开始时已连续异常退出的次数 k；本局再异常退出，下次启动读到 k 就是第 k + 1 次。正常结束删标记 = 清零。
// 测试开关 DM_TEST_CRASH_MARK=<n>（n ≥ 1）：当作有残留标记、本局是第 n 次连续异常退出之后的启动（不读盘上的标记；照常写标记、照常关实验项写回设置）。只读一次环境变量，不常驻分配
// Crash fence; the interface is in dm_crashfence.h.
// What counts as a "normal end": the host's graceful exit (dm_host_shutdown: iOS scene disconnect / app termination, Mac window close, SIGTERM) and iOS going to the background. Going to the background clears too, because iOS kills background processes
// (the player swiping the app away in the switcher, the system reclaiming memory) with a plain SIGKILL and no callback at all; without clearing, such normal ends would count as crashes. Coming back to the foreground writes the marker again. A watchdog kill, a crash or a power loss in the foreground leaves the marker behind.
// Streak: the marker records k, the consecutive abnormal exits seen when this run started; if this run also exits abnormally, the next start reads k and this is number k + 1. A normal end deletes the marker = back to zero.
// Test switch DM_TEST_CRASH_MARK=<n> (n >= 1): act as if a marker were left over and this start follows the n-th consecutive abnormal exit (the marker on disk is not read; the marker is written and experimental features are turned off and saved as usual). The environment variable is read once, nothing stays allocated
#include "dm_crashfence.h"
#include "dm_custom.h"
#include "dm_i18n.h"
#include "hle.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_started, g_streak, g_marked;
static _Atomic int g_notice;

static int marker_path(char *p, size_t n) {
    char d[1024]; if (!dm_custom_dir(d, sizeof d)) return 0;
    return snprintf(p, n, "%s/dm_running.txt", d) < (int)n;
}
static int marker_write(int streak) {   // 先写临时文件再换名：写到一半被杀不会留下半个标记 | write a temporary file first, then rename: being killed halfway leaves no half marker
    char p[1100], t[1120]; if (!marker_path(p, sizeof p)) return 0;
    snprintf(t, sizeof t, "%s.tmp", p); FILE *f = fopen(t, "w"); if (!f) return 0;
    int ok = fprintf(f, "streak=%d\n", streak) > 0; ok = fclose(f) == 0 && ok;
    return ok && rename(t, p) == 0;
}
// 读残留：-1 = 没有标记；否则标记里的 k（坏内容按 0 算：标记在，至少是一次异常退出） | read the leftover: -1 = no marker; otherwise k from the marker (bad contents count as 0: the marker exists, so at least one abnormal exit)
static int marker_read(void) {
    char p[1100]; if (!marker_path(p, sizeof p)) return -1;
    FILE *f = fopen(p, "r"); if (!f) return -1;
    char ln[64] = {0}; int k = 0; if (fgets(ln, sizeof ln, f) && !strncmp(ln, "streak=", 7)) { k = atoi(ln + 7); if (k < 0 || k > 1000000) k = 0; }
    fclose(f); return k;
}

int dm_crashfence_start(void) {
    if (g_started) return g_streak;
    g_started = 1;
    char p[1100]; if (!marker_path(p, sizeof p)) { LOG("【红】崩溃围栏：没有设置目录，不写运行标记"); return 0; }
    const char *tm = getenv("DM_TEST_CRASH_MARK"); int test = tm && atoi(tm) >= 1;
    int k = test ? atoi(tm) - 1 : marker_read();
    g_streak = k < 0 ? 0 : k + 1;
    if (g_streak) LOG("崩溃围栏：上一局异常退出（第 %d 次连续）%s", g_streak, test ? "（测试开关 DM_TEST_CRASH_MARK）" : "");
    else LOG("崩溃围栏：上一局正常结束");
    g_marked = marker_write(g_streak);
    if (!g_marked) LOG("【红】崩溃围栏：运行标记写不进 %s", p);
    if (g_streak >= 2) {
        char what[256]; int n = dm_custom_safe_mode(what, sizeof what);
        atomic_store(&g_notice, 1);
        LOG("崩溃围栏：连续 %d 次异常退出，关闭全部实验性功能（%s；改了 %d 项%s），主菜单提示一次", g_streak, what, n, n ? "，已写回设置文件" : "");
    }
    return g_streak;
}
void dm_crashfence_clean(const char *why) {
    if (!g_marked) return;
    char p[1100]; if (!marker_path(p, sizeof p)) return;
    int ok = unlink(p) == 0; g_marked = !ok; if (ok) g_streak = 0;
    LOG("崩溃围栏：%s，清除运行标记%s", why ? why : "正常结束", ok ? "" : "【红】失败");
}
void dm_crashfence_resume(void) {
    if (!g_started || g_marked) return;
    g_marked = marker_write(g_streak);
    LOG("崩溃围栏：回前台，重写运行标记%s", g_marked ? "" : "【红】失败");
}
int dm_crashfence_notice_take(void) { return atomic_exchange(&g_notice, 0); }
void dm_crashfence_main_menu(void) {
    if (!dm_crashfence_notice_take()) return;
    LOG("崩溃围栏：主菜单提示「已关闭实验性功能」");
    front_notice(dm_ui("dmSafeModeTitle"), dm_ui("dmSafeModeMsg"));
}
#ifndef DM_CRASHFENCE_TEST   // 单测直接包含本文件并自带强定义（t_crashfence.c） | unit tests include this file and bring their own strong definition
__attribute__((weak)) void front_notice(const char *title, const char *msg) { LOG("front_notice：前端未接（缺省弱实现）：%s %s", title, msg); }
#endif
