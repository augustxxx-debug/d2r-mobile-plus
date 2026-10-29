// 安卓性能提示（ADPF）与系统温控（perfhint_android.c）。
// Android performance hints (ADPF) and system thermal status (perfhint_android.c).
#pragma once
#include <stdint.h>
#include <sys/types.h>
#define DM_PERFHINT_MAX_THREADS 4
// 出图线程每帧调一次（diag.c dm_perf_present，覆盖那里的弱定义）；目标 = 当前帧率档 dm_fps_setting | Called once per frame by the present thread (diag.c dm_perf_present, overriding the weak definition there); target = frame-rate mode dm_fps_setting
void dm_perfhint_frame(void);
// 同上，帧率由调用方给（单测用） | As above with the frame rate given by the caller (for tests)
void dm_perfhint_frame_at(int fps);
// 从采样窗口里挑会话线程：忙碌时长不低于最忙线程的 1/10，从忙到闲，最多 max 个；返回个数 | Pick session threads from a sampling window: busy time at least 1/10 of the busiest, busiest first, at most max; returns the count
int dm_perfhint_pick(const pid_t *tids, const uint64_t *busy_ns, int n, pid_t *out, int max);
// AThermal 状态（0 NONE … 6 SHUTDOWN；负数 = 读不到）换成 iOS thermalState 同一套 0–3 档（−1 = 未读） | Map an AThermal status (0 NONE … 6 SHUTDOWN; negative = unreadable) to iOS thermalState's 0–3 levels (−1 = not read)
int dm_thermal_android_level(int status);
int dm_thermal_now(void);
