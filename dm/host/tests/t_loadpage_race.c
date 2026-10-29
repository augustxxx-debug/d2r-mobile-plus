// 加载页核心的跨线程回归（外审 EXT6-01），要在 ThreadSanitizer 下跑（Makefile 的 t_loadpage_race 用 -fsanitize=thread）：
//   场景一「导入写 + UI 读」：一条线程反复 begin / set（带字节细节）/ note / fail / finish / prompt，UI 线程在锁内取文本 + 布局 + 最近一行；
//   场景二「社区写 + UI 读」：一条线程反复 community / copied / pad，UI 线程同上并取复制值。
// 两个场景同时跑 2 万轮。TSan 不报 = 通过。-DDM_LOADPAGE_TEST_NO_LOCK 编出的对照版应当报（证明这个测试抓得到竞争）。
// Cross-thread regression for the loading page core (external review EXT6-01), run under ThreadSanitizer (the Makefile's t_loadpage_race builds with -fsanitize=thread):
//   scenario 1 "import writes + UI reads": one thread loops begin / set (with byte details) / note / fail / finish / prompt; the UI thread takes text + layout + the last line under the lock;
//   scenario 2 "community writes + UI reads": one thread loops community / copied / pad; the UI thread as above plus the copy value.
// Both scenarios run 20,000 rounds at once. No TSan report = pass. The control built with -DDM_LOADPAGE_TEST_NO_LOCK should report (shows this test catches the races).
#include "../dm_loadpage.c"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
const char *dm_ui(const char *key) { return key; }
const char *dm_ui_group(const char *key, int group) { (void)group; return key; }
enum { ROUNDS = 20000 };
static _Atomic int stop;
static void *importer(void *a) {
    (void)a; int kinds[3] = { DM_LP_PATCH, DM_LP_SHADERS, DM_LP_INSTALL };
    for (int i = 0; i < ROUNDS; i++) {
        dm_loadpage_begin(kinds, 3, NULL, (uint64_t)i * 10);
        dm_lp_detail d = { i, 6931, (int64_t)i * 1000, 6931000 }; dm_loadpage_set(1, i / (double)ROUNDS, &d, (uint64_t)i * 10 + 5);
        dm_loadpage_note(i & 1 ? "正文替换" : NULL);
        if (i % 7 == 0) dm_loadpage_fail("磁盘空间不足", (uint64_t)i * 10 + 7); else if (i % 5 == 0) dm_loadpage_finish((uint64_t)i * 10 + 8);
        if (i % 11 == 0) dm_loadpage_prompt("dmLoadPermTitle", "body", "dmLoadPermHint");
    }
    return NULL;
}
static void *community(void *a) {
    (void)a;
    for (int i = 0; i < ROUNDS; i++) {
        if (i & 1) dm_loadpage_community("1129913678", "https://github.com/a/b", "https://d2rplus.augustusplay.com/"); else dm_loadpage_community(NULL, "https://github.com/c/d", NULL);
        dm_loadpage_copied(i & 2, i & 4, (uint64_t)i); dm_loadpage_pad(i & 8);
    }
    return NULL;
}
static void *ui(void *a) {
    (void)a; dm_lp_screen s = { 832.5, 468.3, 0, 0, 0, 0 }; size_t sum = 0; int n = 0;
    while (!stop) {
        dm_loadpage_lock();   // 平台取一帧的样子：一把锁里拿齐文本、布局和要用的指针 | how a platform takes one frame: text, layout and the pointers it uses under one lock
        dm_lp_text t; dm_lp_layout L; dm_loadpage_text(&t, (uint64_t)n); dm_loadpage_layout(&s, &L);
        sum += strlen(t.title) + strlen(t.comm1) + strlen(dm_loadpage_copy_value()) + strlen(dm_loadpage_last_line()) + (size_t)L.comm + (size_t)dm_loadpage_music_wanted();
        dm_loadpage_unlock(); n++;
    }
    printf("t_loadpage_race：UI 读了 %d 帧（校验和 %zu），两条写线程各 %d 轮\n", n, sum, ROUNDS);
    return NULL;
}
int main(void) {
    pthread_t a, b, c;
    pthread_create(&c, NULL, ui, NULL); pthread_create(&a, NULL, importer, NULL); pthread_create(&b, NULL, community, NULL);
    pthread_join(a, NULL); pthread_join(b, NULL); stop = 1; pthread_join(c, NULL);
    return 0;
}
