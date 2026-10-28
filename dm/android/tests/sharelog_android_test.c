// 分享诊断日志（安卓）的脱敏与组装：规则照 iOS dm_diagnostic_scrub.h；只用临时目录。cc -I.. sharelog_android_test.c ../sharelog_android.c -o <仓外产物> 后运行。
// Scrubbing and assembly of Android share diagnostics: rules as iOS dm_diagnostic_scrub.h; temporary directories only. Build: cc -I.. sharelog_android_test.c ../sharelog_android.c -o <external output>
#include "../sharelog_android.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static char *scrub(const char *s, const char *const *names, int n) { size_t l; return dm_sharelog_scrub(s, strlen(s), "/data/user/0/com.xiaoboluo.d2r", "com.xiaoboluo.d2r", names, n, &l); }
static int has(const char *t, const char *x) { return strstr(t, x) != NULL; }
int main(void) {
    char *o;
    o = scrub("path /data/user/0/com.xiaoboluo.d2r/files/dm/boot.log and /storage/emulated/0/Download/x", NULL, 0);
    assert(has(o, "path ~/files/dm/boot.log") && has(o, "~/Download/x") && !has(o, "xiaoboluo")); free(o);
    o = scrub("save at /files/save/Hanna/Hanna.d2s then Hanna said hi; Hannah stays; /save.test.work/Bob/ Bob", NULL, 0);
    assert(has(o, "/files/save/<character>/<character>.d2s") && has(o, "<character> said hi") && has(o, "Hannah stays") && has(o, "/save.test.work/<character>/ <character>")); free(o);
    o = scrub("/save/Settings/x /save/ModernSharedStash1/y", NULL, 0); assert(has(o, "/save/Settings/x") && has(o, "ModernSharedStash1")); free(o);
    { const char *kn[] = { "Zed" }; o = scrub("hero Zed logged; Zedd not", kn, 1); assert(has(o, "hero <character> logged; Zedd not")); free(o); }
    o = scrub("home /Users/augustwang/x mail someone.x@example.com id 12345678-1234-1234-1234-123456789abc dev 0123ABCD-0123456789ABCDEF ok 0123ABCD-0123456789ABCDEF1 keep deadbeef-cafe", NULL, 0);
    assert(has(o, "home <redacted>/x mail <redacted> id <redacted> dev <redacted> ok 0123ABCD-0123456789ABCDEF1 keep deadbeef-cafe")); free(o);
    o = scrub("屏幕：My Monitor 27，x 屏幕 2622x1206，y 屏幕 Living Room，z", NULL, 0);
    assert(has(o, "屏幕：<display>，x 屏幕 2622x1206，y 屏幕 <display>，z")); free(o);
    // 组装：本局 / 上一局 / 退出原因 / 角色目录名 | assembly
    char dir[] = "/tmp/dm_sharelog_XXXXXX"; assert(mkdtemp(dir)); char rt[300], fs[300], p[600];
    snprintf(fs, sizeof fs, "%s/files", dir); snprintf(rt, sizeof rt, "%s/files/dm", dir); snprintf(p, sizeof p, "%s/save/Ann", fs); mkdir(fs, 0700); mkdir(rt, 0700); snprintf(p, sizeof p, "%s/save", fs); mkdir(p, 0700); snprintf(p, sizeof p, "%s/save/Ann", fs); mkdir(p, 0700);
    snprintf(p, sizeof p, "%s/save/Ann/Ann.d2s", fs); fclose(fopen(p, "w"));
    snprintf(p, sizeof p, "%s/boot.log", rt); FILE *f = fopen(p, "w"); fputs("boot: 显示版本 1.0.0+91 · 674f5d5\nNVN caps device=\"Adreno (TM) 740\" vendor=0x5143\n帧率档：生效 60\nAnn entered camp\n", f); fclose(f);
    snprintf(p, sizeof p, "%s/boot.previous.log", rt); f = fopen(p, "w"); for (int i = 0; i < 40000; i++) fprintf(f, "line %d filler filler filler\n", i); fputs("帧率档：生效 30 (prev)\n", f); fclose(f);
    snprintf(p, sizeof p, "%s/exit_reasons.txt", rt); f = fopen(p, "w"); fputs("reason=4 (CRASH_NATIVE) time=2026-10-28T14:30 desc=SIGSEGV\n", f); fclose(f);
    char out[600]; snprintf(out, sizeof out, "%s/diag.txt", dir);
    long n = dm_sharelog_build(rt, fs, out, "1.0.0+91 · 674f5d5", "AYN Thor / Android 13 / Adreno 740", "com.xiaoboluo.d2r"); assert(n > 0);
    f = fopen(out, "rb"); char *t = malloc((size_t)n + 1); assert(fread(t, 1, (size_t)n, f) == (size_t)n); t[n] = 0; fclose(f);
    assert(has(t, "显示版本 1.0.0+91") && has(t, "CRASH_NATIVE") && has(t, "帧率档：生效 60") && has(t, "帧率档：生效 30 (prev)") && has(t, "<character> entered camp") && !has(t, "Ann entered"));
    assert(has(t, "omitted prefix bytes current=0 previous=") && !has(t, "previous=0.")); assert(has(t, "（没有「崩溃围栏：」行）"));
    free(t);
    puts("PASS sharelog_android: scrub rules (app id, home, sdcard, character names, /Users, e-mail, UUID, 8-16 hex, display) and assembly (header, exit reasons, key readings, two logs, tail limit)");
    return 0;
}
