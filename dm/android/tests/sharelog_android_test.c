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
    o = scrub("work /storage/emulated/10/Download/x and /storage/emulated/0/a, keep /storage/emulated/legacy", NULL, 0); assert(has(o, "work ~/Download/x and ~/a, keep /storage/emulated/legacy") && !has(o, "emulated/10")); free(o);   // 五审 A5-04 | fifth audit A5-04
    o = scrub("save at /files/save/Hanna/Hanna.d2s then Hanna said hi; Hannah stays; /save.test.work/Bob/ Bob", NULL, 0);
    assert(has(o, "/files/save/<character>/<character>.d2s") && has(o, "<character> said hi") && has(o, "Hannah stays") && has(o, "/save.test.work/<character>/ <character>")); free(o);
    o = scrub("/save/Settings/x /save/ModernSharedStash1/y", NULL, 0); assert(has(o, "/save/Settings/x") && has(o, "ModernSharedStash1")); free(o);
    { const char *kn[] = { "Zed" }; o = scrub("hero Zed logged; Zedd not", kn, 1); assert(has(o, "hero <character> logged; Zedd not")); free(o); }
    // 中文标点挨着角色名（奥丁真机导出 2026-10-28 漏了 4 处：「角色「X」」「个人仓库：X 还没有」）：中文标点不是字母，要替换
    // Chinese punctuation next to a character name (the Thor export of 2026-10-28 leaked 4: "角色「X」", "个人仓库：X 还没有"): CJK punctuation is not a letter, so the name is replaced
    { const char *kn[] = { "Galkamir" }; o = scrub("秘境：本局没有资格（难度 0，角色「Galkamir」）\n个人仓库：Galkamir 还没有翻过页\n刷怪积分牌：「Galkamir」还没有记录\n角色Galkamir进入营地", kn, 1);
      assert(has(o, "角色「<character>」") && has(o, "个人仓库：<character> 还没有") && has(o, "刷怪积分牌：「<character>」") && has(o, "角色<character>进入营地") && !has(o, "Galkamir")); free(o); }
    // 一审 A1：中文 / 日文 / 韩文名不靠空格分词，紧挨汉字也要替换；拉丁名挨着汉字（换文种）也算词边界；同文种字母相连仍不算（Ann ≠ Anna，Иван ≠ Иванов）；
    // 名字互为子串时先换长的（张三丰不能变成「<character>丰」）
    // First audit A1: Chinese / Japanese / Korean names are not space-delimited and are replaced even right next to Han text; a Latin name next to Han (a script change) is at a
    // word boundary too; letters of the same kind still join (Ann != Anna, Иван != Иванов); when one name contains another the longer one goes first (张三丰 must not become "<character>丰")
    { const char *kn[] = { "张三" }; o = scrub("角色张三进入营地\n张三abc 与 abc张三\n「张三」，张三：", kn, 1);
      assert(has(o, "角色<character>进入营地") && has(o, "<character>abc 与 abc<character>") && has(o, "「<character>」，<character>：") && !has(o, "张三")); free(o); }
    { const char *kn[] = { "张三", "张三丰" }; o = scrub("张三丰来了，张三走了", kn, 2); assert(has(o, "<character>来了，<character>走了") && !has(o, "丰")); free(o); }
    o = scrub("/save/张三/a.d2s /save/张三丰/b.d2s 张三丰回城", NULL, 0); assert(has(o, "/save/<character>/a.d2s /save/<character>/b.d2s <character>回城") && !has(o, "丰")); free(o);
    { const char *kn[] = { "갈카미르", "Ann", "Иван", "ガルカ" }; o = scrub("갈카미르가 마을에 / Anna stays, Ann goes / Иванов stays, Иван goes / ガルカミール", kn, 4);
      assert(has(o, "<character>가 마을에") && has(o, "Anna stays, <character> goes") && has(o, "Иванов stays, <character> goes") && has(o, "<character>ミール")); free(o); }
    o = scrub("home /Users/someone/x mail someone.x@example.com id 12345678-1234-1234-1234-123456789abc dev 0123ABCD-0123456789ABCDEF ok 0123ABCD-0123456789ABCDEF1 keep deadbeef-cafe", NULL, 0);
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
