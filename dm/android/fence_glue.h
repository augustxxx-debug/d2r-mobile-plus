// 崩溃围栏（闸 1）在安卓的接线：Activity 的生命周期事件 → dm_crashfence_*。纯函数，jni_android.c 调；tests/fence_glue_test.c 验映射。
// 事件号同 nativeFocus：0 失焦、1 后台、2 前台、3 激活。iOS 的对应：sceneDidEnterBackground → background，sceneWillEnterForeground → resume，
// applicationDidReceiveMemoryWarning → note_memory（这里由 onTrimMemory 触发），场景断开 / App 终止 → dm_host_shutdown（优雅退出，清标记）。
// Android wiring of the crash fence (release gate 1): Activity lifecycle events -> dm_crashfence_*. Pure function called by jni_android.c; tests/fence_glue_test.c checks the mapping.
// Event numbers as nativeFocus: 0 unfocused, 1 background, 2 foreground, 3 active. iOS counterparts: sceneDidEnterBackground -> background, sceneWillEnterForeground -> resume,
// applicationDidReceiveMemoryWarning -> note_memory (here from onTrimMemory), scene disconnect / app termination -> dm_host_shutdown (graceful exit, marker cleared).
#pragma once
typedef struct dm_fence_hooks { void (*background)(void); void (*resume)(void); } dm_fence_hooks;
// 返回调了哪个：1 后台、2 前台、0 没调 | returns which hook ran: 1 background, 2 resume, 0 none
static inline int dm_android_fence_event(int ev, const dm_fence_hooks *h) {
    if (ev == 1) { h->background(); return 1; }
    if (ev == 2 || ev == 3) { h->resume(); return 2; }
    return 0;
}
// onTrimMemory 的级别：≥ TRIM_MEMORY_RUNNING_CRITICAL(15) 才算系统内存告警（RUNNING_LOW 10 / RUNNING_MODERATE 5 只是提示；后台档 20 / 40 / 60 / 80 是进程排序，不是本进程缺内存）
// onTrimMemory levels: only >= TRIM_MEMORY_RUNNING_CRITICAL (15) counts as a system memory warning (RUNNING_LOW 10 / RUNNING_MODERATE 5 are hints; background levels 20 / 40 / 60 / 80 are process ranking, not this process short of memory)
static inline int dm_android_trim_is_warning(int level) { return level == 15 || level == 10; }
