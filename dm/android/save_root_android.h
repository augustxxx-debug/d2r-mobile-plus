// 这一局的存档根：带 DM_SAVE_TEST（files/dm/env）就是 files/save.test，不碰玩家的 files/save（同 iOS main.m 的 Documents/save.test）；崩溃围栏判测试局另有一套，不看这里。
// This run's save root: with DM_SAVE_TEST (files/dm/env) it is files/save.test and the player's files/save is left alone (as iOS main.m's Documents/save.test); the crash fence's test-run check is separate and does not look here.
#ifndef DM_SAVE_ROOT_ANDROID_H
#define DM_SAVE_ROOT_ANDROID_H
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static inline int dm_android_save_root(char *out, size_t size, const char *files) {
    int test = getenv("DM_SAVE_TEST") != NULL;
    snprintf(out, size, "%s/%s", files, test ? "save.test" : "save");
    return test;
}
#endif
