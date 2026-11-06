// 崩溃围栏安卓接线的映射：生命周期事件 → background / resume；onTrimMemory 级别 → 内存告警。cc fence_glue_test.c -o <仓外产物> 后运行。
// Mapping of the Android crash-fence wiring: lifecycle events -> background / resume; onTrimMemory level -> memory warning. Build with cc fence_glue_test.c -o <external output> and run.
#include "../fence_glue.h"
#include <assert.h>
#include <stdio.h>
static int bg, rs;
static void on_bg(void) { bg++; } static void on_rs(void) { rs++; }
int main(void) {
    dm_fence_hooks h = { on_bg, on_rs };
    assert(dm_android_fence_event(0, &h) == 0 && !bg && !rs);        // 失焦：不动标记（画面还在动）| unfocused: marker untouched (still presenting)
    assert(dm_android_fence_event(1, &h) == 1 && bg == 1 && !rs);     // 后台 | background
    assert(dm_android_fence_event(2, &h) == 2 && bg == 1 && rs == 1); // 前台 | foreground
    assert(dm_android_fence_event(3, &h) == 2 && rs == 2);            // 激活也算前台（UI 合并 2/3 时不漏恢复）| active counts as foreground too
    assert(dm_android_fence_event(7, &h) == 0 && bg == 1 && rs == 2);
    assert(dm_android_trim_is_warning(15) && dm_android_trim_is_warning(10));
    assert(!dm_android_trim_is_warning(5) && !dm_android_trim_is_warning(20) && !dm_android_trim_is_warning(40) && !dm_android_trim_is_warning(80));
    puts("PASS fence_glue: lifecycle events map to crash-fence background / resume; trim levels");
    return 0;
}
