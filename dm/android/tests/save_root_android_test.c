/* 存档根：测试局不落到玩家存档；DM_SAVE_TEST 的值不看，同 iOS 只看有没有。
 * Save root: a test run stays out of the player's save; the value of DM_SAVE_TEST does not matter, as on iOS only its presence does. */
#include "../save_root_android.h"
#include <assert.h>
#include <string.h>

int main(void) {
    char out[64];
    unsetenv("DM_SAVE_TEST");
    assert(!dm_android_save_root(out, sizeof out, "/f") && !strcmp(out, "/f/save"));
    setenv("DM_SAVE_TEST", "1", 1);
    assert(dm_android_save_root(out, sizeof out, "/f") && !strcmp(out, "/f/save.test"));
    setenv("DM_SAVE_TEST", "0", 1);
    assert(dm_android_save_root(out, sizeof out, "/f") && !strcmp(out, "/f/save.test"));
    puts("PASS save_root_android");
    return 0;
}
