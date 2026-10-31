// 存档管理的游戏内面板（格式与流程在 dm_savepkg.c）。一套通用列表框（标题 / 副文 / 6 行 / 页码 / 备注 / 两颗按钮），导出范围、预览、备份列表都用它；布局 touch_layout.py _savebox，平时在屏外、字空着。
// 只在前端可用（dm_uiscreen_ingame 为 0）：局内自定义页这一行灰显。系统面板（选文件 / 保存）由外壳做（dm_savemgr_shell），测试局用 DM_TEST_SAVEPKG_IN / DM_TEST_SAVEPKG_OUT 绕过。
// 导入确认后提示框「重新启动后生效」，确定后走 dm_host_shutdown（崩溃围栏记正常结束），下次启动 dm_savemgr_boot 换入，第一次到前端显示结果。
// 弹窗机制借社区提示框：dm_community_modal / 命中 / 点按 / 闸门都转到这里（dm_community.c），所以 dm_cursor.c、hle_hid.c 不用改。耗时的事放后台线程（dm_savemgr_job）。日志固定前缀「存档管理：」，不写角色名。
// In-game panel of save management (format and flow in dm_savepkg.c). One generic list box (title / subtitle / 6 rows / page / note / two buttons) serves the export scope, the preview and the backup list; layout touch_layout.py _savebox, off screen with empty text.
// Front end only (dm_uiscreen_ingame is 0): in a run the Custom page row is greyed. System panels (pick / save a file) belong to the shell (dm_savemgr_shell); test runs bypass them with DM_TEST_SAVEPKG_IN / DM_TEST_SAVEPKG_OUT.
// After an import is confirmed the box says it applies after a restart; OK goes through dm_host_shutdown (the crash fence records a normal end), dm_savemgr_boot applies it at the next start and the result shows on reaching the front end.
// The modal mechanism is borrowed from the community box: dm_community_modal / hit answer / tap / gate forward here (dm_community.c), so dm_cursor.c and hle_hid.c stay untouched. Slow work runs on a background thread. Log prefix "存档管理：", never a character name.
#include "dm_savepkg.h"
#include "dm_community.h"
#include "dm_custom.h"
#include "dm_i18n.h"
#include "dm_input.h"
#include "dm_notice_geometry.h"
#include "dm_stick_nav.h"
#include "dm_uiscreen.h"
#include "dm_widget.h"
#include "hle.h"
#include <dirent.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#include <unistd.h>

// dm_community.c 的控件小工具 | widget helpers of dm_community.c
uint8_t *co_base(void); uint64_t co_find(uint64_t w, const char *name, int dep); void co_place(uint64_t w, int x, int y); void co_text(uint8_t *b, uint64_t w, const char *s); void co_tile(uint8_t *b, uint64_t w, const int want[4]);
void co_rect_lines(uint8_t *b, uint64_t p, const char *pre, const char *fh, const char *fv, int x, int y, int wd, int ht, int t); void co_rect_hide(uint64_t p, const char *pre); void co_origin(uint64_t p, int xr, int *ox, int *oy, float *s); int co_has(uint64_t w, uint64_t target, int dep);
int dm_padmode_current(void); uint64_t dm_padmode_page_keys(uint64_t); int dm_save_snapshot(const char *dst); void dm_host_shutdown(const char *why); int dm_custom_dir(char *p, size_t n); void dm_custom_reload(void);

// 「保留两份」：T0 探针（改名后的角色能列出、进局、存盘、再进）。不过就改 0，界面只给「覆盖 / 跳过」（主会话 D5） | keep-both: per the T0 probe; 0 leaves only overwrite / skip
#ifndef DM_SAVEMGR_KEEPBOTH
#define DM_SAVEMGR_KEEPBOTH 1
#endif
#define PER_PAGE 6
#define MAX_ROWS (DM_PKG_MAX_CHARS + DM_PKG_MAX_STASH + 2)
#define BOX_W 1800
typedef struct { char text[200], tag[96]; int kind, idx, on; } row_t;   // kind：见各页 | kind: per page
enum { S_NONE, S_HOME, S_EXPORT, S_WORK, S_PREVIEW, S_RESTART, S_MSG, S_BKLIST, S_BKITEM, S_BKDEL };
enum { RK_PLAIN, RK_CHAR, RK_STASH, RK_SETTINGS };
enum { J_NONE, J_EXPORT, J_INSPECT, J_BACKUP };

static _Atomic int g_st, g_shown, g_pick_wait;    // g_shown：控件已摆好（命中用） | g_shown: widgets are placed (for hit testing)
static row_t *g_rows; static int g_nrows, g_page, g_foc, g_home_import;   // g_foc：0..本页行数-1 = 行，之后是按钮 | g_foc: 0..rows on page-1 = row, then buttons
static char g_title[160], g_sub[260], g_note[200], g_btn[2][48]; static int g_nbtn;
static dm_pkg_view g_view; static int g_view_ok, g_restore;
static dm_pkg_backup g_bk[64]; static int g_nbk, g_bk_sel;
static dm_pkg_source g_src = { "macos", "", "", "", "3.3.94012" };
static char g_src_buf[4][64];
static void (*g_pick)(void), (*g_share)(const char *path);
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static char g_picked[1400]; static _Atomic int g_picked_new, g_share_done;   // 外壳回调：选好了 / 系统面板关了（1 = 存了，-1 = 没存）| shell callbacks: picked / the system panel closed (1 saved, -1 not)
static char g_share_path[1400];
static _Atomic int g_job_done, g_job_rc; static int g_job; static char g_job_arg[1400];
static const char **g_sel_names; static int g_sel_n, g_sel_stash, g_sel_settings;

void dm_savemgr_shell(void (*pick)(void), void (*share)(const char *)) { g_pick = pick; g_share = share; }
void dm_savemgr_set_source(const char *platform, const char *commit, const char *build, const char *version) {
    const char *in[4] = { platform, commit, build, version }; for (int i = 0; i < 4; i++) strlcpy(g_src_buf[i], in[i] ? in[i] : "", sizeof g_src_buf[i]);
    g_src.platform = g_src_buf[0]; g_src.commit = g_src_buf[1]; g_src.build = g_src_buf[2]; g_src.version = g_src_buf[3];
}
static const char *save_dir(void) { return getenv("DM_SAVE"); }
// 启动期：挂存档与云同步之前（iOS boot_thread、Mac main、安卓 nativeStart）。清上次导出的残留，换入待应用的导入 | at boot, before mount and cloud sync: clear the last export's leftovers and apply a pending import
void dm_savemgr_boot(void) {
    const char *sv = save_dir(); if (!sv) return; char d[1400]; snprintf(d, sizeof d, "%s.export.tmp", sv); dm_savepkg_rmtree(d);
    char cd[1100]; const char *custom = dm_custom_dir(cd, sizeof cd) ? cd : NULL; if (dm_savepkg_apply_pending(sv, custom, time(NULL), &g_src) == 1) dm_custom_reload();
}
int dm_savemgr_modal(void) { return atomic_load(&g_st) != S_NONE; }
int dm_savemgr_shown(void) { return atomic_load(&g_shown); }
// 外壳回调（任意线程）| shell callbacks (any thread)
void dm_savemgr_picked(const char *path) { pthread_mutex_lock(&g_mu); strlcpy(g_picked, path ? path : "", sizeof g_picked); pthread_mutex_unlock(&g_mu); atomic_store(&g_picked_new, 1); }
void dm_savemgr_share_done(int saved) { atomic_store(&g_share_done, saved ? 1 : -1); }

static uint8_t *base_(void) { return co_base(); }
static const char *S(const char *k) { return dm_str(base_(), k); }
static void set_box(const char *title, const char *sub, const char *note, const char *b0, const char *b1) { strlcpy(g_title, title ? title : "", sizeof g_title); strlcpy(g_sub, sub ? sub : "", sizeof g_sub); strlcpy(g_note, note ? note : "", sizeof g_note); g_nbtn = 0; if (b0) strlcpy(g_btn[g_nbtn++], b0, sizeof g_btn[0]); if (b1) strlcpy(g_btn[g_nbtn++], b1, sizeof g_btn[0]); }
static void rows_clear(void) { if (!g_rows) g_rows = calloc(MAX_ROWS, sizeof *g_rows); g_nrows = 0; g_page = 0; g_foc = 0; }
static row_t *row_add(const char *text, const char *tag, int kind, int idx) { if (g_nrows >= MAX_ROWS) return NULL; row_t *r = &g_rows[g_nrows++]; memset(r, 0, sizeof *r); strlcpy(r->text, text, sizeof r->text); strlcpy(r->tag, tag ? tag : "", sizeof r->tag); r->kind = kind; r->idx = idx; return r; }
static void go(int st) { atomic_store(&g_st, st); atomic_store(&g_shown, 0); }
static const char *err_key(int rc) { switch (rc) { case DM_PKG_E_ZIP: case DM_PKG_E_NOTOURS: return "dmSaveErrZip"; case DM_PKG_E_CHECK: return "dmSaveErrCheck"; case DM_PKG_E_TOONEW: return "dmSaveErrNew"; case DM_PKG_E_LIMIT: case DM_PKG_E_PATH: return "dmSaveErrBad"; case DM_PKG_E_SPACE: return "dmSaveErrSpace"; case DM_PKG_E_STARFULL: return "dmSaveErrStar"; case DM_PKG_E_CONVERT: return "dmSaveErrConvert"; case DM_PKG_E_COOP: return "dmSaveErrCoop"; default: return "dmSaveErrIo"; } }
static int g_msg_back;   // 消息框确定后回哪一页（S_NONE = 收起）| where OK on the message box returns to
static void show_fail(int rc, int back) { rows_clear(); set_box(S("dmSaveFailTitle"), S(err_key(rc)), S("dmSaveFailSub"), S("dmSaveOk"), NULL); g_msg_back = back; go(S_MSG); LOG("存档管理：失败提示 代码 %d", rc); }
static void show_msg(const char *title, const char *sub, int back) { rows_clear(); set_box(title, sub, NULL, S("dmSaveOk"), NULL); g_msg_back = back; go(S_MSG); }
static void show_home(void);

// ---- 后台活 | Background work
static void iso_stamp(char *o, size_t n) { time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm); strftime(o, n, "%Y%m%d-%H%M%S", &tm); }
static void *job_main(void *arg) {
    (void)arg; int rc = DM_PKG_E_IO; const char *sv = save_dir(); char cd[1100]; const char *custom = dm_custom_dir(cd, sizeof cd) ? cd : NULL;
    if (g_job == J_INSPECT) { dm_pkg_view v; rc = dm_savepkg_inspect(g_job_arg, sv, g_restore, &v); if (!rc) { g_view = v; g_view_ok = 1; } else dm_savepkg_view_free(&v); }
    else if (g_job == J_BACKUP) { char d[1400], snap[1500]; snprintf(d, sizeof d, "%s.export.tmp", sv); dm_savepkg_rmtree(d); mkdir(d, 0755); snprintf(snap, sizeof snap, "%s/snap", d); rc = dm_save_snapshot(snap) ? DM_PKG_E_IO : dm_savepkg_backup_tree(sv, snap, "manual", time(NULL), custom, &g_src, NULL, 0); dm_savepkg_rmtree(d); }
    else if (g_job == J_EXPORT) { char d[1400], snap[1500], ts[32]; snprintf(d, sizeof d, "%s.export.tmp", sv); dm_savepkg_rmtree(d); mkdir(d, 0755); snprintf(snap, sizeof snap, "%s/snap", d); iso_stamp(ts, sizeof ts); snprintf(g_share_path, sizeof g_share_path, "%s/D2RMobilePlus_saves_%s.zip", d, ts);
        if (dm_save_snapshot(snap)) rc = DM_PKG_E_IO; else { dm_pkg_export_opts o; memset(&o, 0, sizeof o); o.names = g_sel_names; o.nnames = g_sel_n; o.stash = g_sel_stash; o.settings = g_sel_settings; o.source_root = sv; o.custom_dir = custom; o.kind = "export"; o.now = time(NULL); rc = dm_savepkg_export(snap, g_share_path, &o, &g_src); } }
    atomic_store(&g_job_rc, rc); atomic_store(&g_job_done, 1); return NULL;
}
static void job_start(int j, const char *arg) { g_job = j; strlcpy(g_job_arg, arg ? arg : "", sizeof g_job_arg); atomic_store(&g_job_done, 0); rows_clear(); set_box(S("dmSaveWorking"), S("dmSaveWorkingSub"), NULL, NULL, NULL); go(S_WORK); pthread_t th; if (pthread_create(&th, NULL, job_main, NULL)) { atomic_store(&g_job_rc, DM_PKG_E_IO); atomic_store(&g_job_done, 1); } else pthread_detach(th); }

// ---- 各页 | Pages
static void fmt_date(uint32_t t, char *o, size_t n) { time_t tt = t; struct tm tm; localtime_r(&tt, &tm); strftime(o, n, "%Y-%m-%d", &tm); }
static void char_row_text(const dm_pkg_char *c, char *o, size_t n) { char d[16], key[16], cl[64]; fmt_date(c->last_played, d, sizeof d); snprintf(key, sizeof key, "dmSaveCls%d", c->cls); strlcpy(cl, S(key), sizeof cl); snprintf(o, n, S("dmSaveRowFmt"), c->name, cl, c->level, d); if (c->hardcore) { strlcat(o, " · ", n); strlcat(o, S("dmSaveStashHard"), n); } }
static void stash_label(const char *nm, char *o, size_t n) { snprintf(o, n, S("dmSaveStashRow"), S(strstr(nm, "HardCore") ? "dmSaveStashHard" : "dmSaveStashSoft")); }
static void show_home(void) { rows_clear(); row_add(S("dmSaveExport"), NULL, RK_PLAIN, 0); row_add(S("dmSaveImport"), NULL, RK_PLAIN, 1); row_add(S("dmSaveBackupNow"), NULL, RK_PLAIN, 2); row_add(S("dmSaveBackupList"), NULL, RK_PLAIN, 3); set_box(S("dmSaveMgrRow"), S("dmSaveMgrSub"), NULL, S("dmSaveBack"), NULL); go(S_HOME); }
static void export_page(void) {
    rows_clear(); const char *sv = save_dir(); DIR *d = opendir(sv); struct dirent *e;
    while (d && (e = readdir(d))) { if (e->d_name[0] == '.' || e->d_name[0] == '_' || dm_savepkg_is_coop_char(sv, e->d_name)) continue; char p[1400]; snprintf(p, sizeof p, "%s/%s/%s.d2s", sv, e->d_name, e->d_name); size_t L = 0; FILE *f = fopen(p, "rb"); if (!f) continue; uint8_t *b = malloc(1 << 20); if (b) { L = fread(b, 1, 1 << 20, f); dm_pkg_char c; memset(&c, 0, sizeof c); if (dm_savepkg_parse_d2s(b, L, &c)) { char t[200]; char_row_text(&c, t, sizeof t); row_t *r = row_add(t, S("dmSaveSelected"), RK_CHAR, 0); if (r) r->on = 1; } free(b); } fclose(f); }
    if (d) closedir(d);
    row_t *r = row_add(S("dmSaveStashRow"), S("dmSaveSelected"), RK_STASH, 0); if (r) { snprintf(r->text, sizeof r->text, "%s", S("dmSaveStashRow")); char *pct = strstr(r->text, " · %s"); if (pct) *pct = 0; r->on = 1; }
    r = row_add(S("dmSaveSettingsRow"), S("dmSaveNotSelected"), RK_SETTINGS, 0);
    set_box(S("dmSaveExport"), S("dmSaveExportSub"), dm_savepkg_coop_count(sv) ? S("dmSaveCoopNote") : NULL, S("dmSaveExportGo"), S("dmSaveBack")); go(S_EXPORT);
}
static void preview_tag(row_t *r) {
    if (r->kind == RK_CHAR) { dm_pkg_char *c = &g_view.chars[r->idx]; char st[80] = "";
        if (c->status == DM_PKG_SAME) { strlcpy(r->tag, S("dmSaveStSame"), sizeof r->tag); return; } if (c->status == DM_PKG_BAD) { strlcpy(r->tag, S("dmSaveStBad"), sizeof r->tag); return; }
        if (c->status == DM_PKG_NEW) { strlcpy(r->tag, S(c->action == DM_PKG_SKIP ? "dmSaveSkip" : "dmSaveStNew"), sizeof r->tag); return; }
        snprintf(st, sizeof st, S("dmSaveStConflict"), c->local_level); snprintf(r->tag, sizeof r->tag, "%s · %s", st, S(c->action == DM_PKG_OVERWRITE ? "dmSaveOverwrite" : c->action == DM_PKG_KEEPBOTH ? "dmSaveKeepBoth" : "dmSaveSkip")); }
    else if (r->kind == RK_STASH) { dm_pkg_stash *x = &g_view.stash[r->idx]; strlcpy(r->tag, x->status == DM_PKG_SAME ? S("dmSaveStSame") : S(x->action == DM_PKG_OVERWRITE ? "dmSaveOverwrite" : "dmSaveSkip"), sizeof r->tag); }
    else if (r->kind == RK_SETTINGS) strlcpy(r->tag, S(g_view.settings_action == DM_PKG_OVERWRITE ? "dmSaveOverwrite" : "dmSaveSkip"), sizeof r->tag);
}
static void preview_page(void) {
    rows_clear(); for (int i = 0; i < g_view.nchars; i++) { char t[200]; dm_pkg_char *c = &g_view.chars[i]; if (c->status == DM_PKG_BAD && !c->level) snprintf(t, sizeof t, "%s", c->name); else char_row_text(c, t, sizeof t); row_t *r = row_add(t, "", RK_CHAR, i); if (r) preview_tag(r); }
    for (int i = 0; i < g_view.nstash; i++) { char t[200]; stash_label(g_view.stash[i].name, t, sizeof t); row_t *r = row_add(t, "", RK_STASH, i); if (r) preview_tag(r); }
    if (g_view.has_settings) { row_t *r = row_add(S("dmSaveSettingsRow"), "", RK_SETTINGS, 0); if (r) preview_tag(r); }
    char when[16]; strlcpy(when, g_view.exported_at, 11); char sub[260]; const char *pl = !strcmp(g_view.platform, "macos") ? "macOS" : g_view.platform;
    // 平台词不进另一端的包（CLAUDE.md §2.8，package_audit.py）：包来自那一端时只说「其他设备」| platform names stay out of the other platform's package: a package from there just says "another device"
#if defined(__ANDROID__)
    if (!strcmp(g_view.platform, "ios") || !strcmp(g_view.platform, "ipados")) pl = S("dmSaveFromOtherDevice"); else if (!strcmp(g_view.platform, "android")) pl = "Android";
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    if (!strcmp(g_view.platform, "android")) pl = S("dmSaveFromOtherDevice"); else if (!strcmp(g_view.platform, "ios")) pl = "iOS"; else if (!strcmp(g_view.platform, "ipados")) pl = "iPadOS";
#else
    if (!strcmp(g_view.platform, "ios")) pl = "iOS"; else if (!strcmp(g_view.platform, "ipados")) pl = "iPadOS"; else if (!strcmp(g_view.platform, "android")) pl = "Android";
#endif
     if (!strcmp(g_view.source_format, "d2r-pc")) pl = S("dmSaveFmtPc"); else if (!strcmp(g_view.source_format, "d2lod")) pl = S("dmSaveFmtLod"); else if (strcmp(g_view.source_format, "d2r-switch-plus")) pl = S("dmSaveFmtOther"); snprintf(sub, sizeof sub, S("dmSavePreviewFrom"), pl, g_view.version[0] ? g_view.version : g_view.build, when);
    const char *note = NULL;
#ifndef __ANDROID__
    if (dm_custom_get("icloud") && !getenv("DM_TEST_ICLOUD_OFF")) note = S("dmCloudSaveOn");
#endif
    set_box(S("dmSaveImport"), sub, note, S("dmSavePreviewGo"), S("dmSaveBack")); go(S_PREVIEW);
}
static void bklist_page(void) {
    g_nbk = dm_savepkg_backup_list(save_dir(), g_bk, 64); rows_clear();
    for (int i = 0; i < g_nbk; i++) { char when[32], sz[24], t[200]; time_t tt = (time_t)g_bk[i].when; struct tm tm; localtime_r(&tt, &tm); strftime(when, sizeof when, "%Y-%m-%d %H:%M", &tm); if (g_bk[i].size >= 1 << 20) snprintf(sz, sizeof sz, "%.1f MB", g_bk[i].size / 1048576.0); else snprintf(sz, sizeof sz, "%lld KB", g_bk[i].size / 1024 + 1); snprintf(t, sizeof t, S("dmSaveBkRow"), when, S(g_bk[i].is_auto ? "dmSaveBkAuto" : "dmSaveBkManual"), g_bk[i].chars, sz); row_add(t, g_bk[i].starred ? "★" : "", RK_PLAIN, i); }
    const char *note = dm_savepkg_coop_count(save_dir()) ? S("dmSaveCoopNote") : NULL;
#ifdef __ANDROID__
    if (!note) note = S("dmAndSaveBackupPath");
#endif
    set_box(S("dmSaveBackupList"), S(g_nbk ? "dmSaveBkKeep" : "dmSaveBkEmpty"), note, S("dmSaveBack"), NULL); go(S_BKLIST);
}
static void bkitem_page(void) { rows_clear(); dm_pkg_backup *b = &g_bk[g_bk_sel]; row_add(S("dmSaveBkRestore"), NULL, RK_PLAIN, 0); row_add(S(b->starred ? "dmSaveBkUnstar" : "dmSaveBkStar"), NULL, RK_PLAIN, 1); row_add(S("dmSaveBkShare"), NULL, RK_PLAIN, 2); row_add(S("dmSaveBkDelete"), NULL, RK_PLAIN, 3); char when[32]; time_t tt = (time_t)b->when; struct tm tm; localtime_r(&tt, &tm); strftime(when, sizeof when, "%Y-%m-%d %H:%M", &tm); set_box(S("dmSaveBackupList"), when, NULL, S("dmSaveBack"), NULL); go(S_BKITEM); }
static void start_import(int restore, const char *path) { g_restore = restore; if (g_view_ok) { dm_savepkg_view_free(&g_view); g_view_ok = 0; } job_start(J_INSPECT, path); }
static void request_pick(void) {
#ifdef DM_QA_BUILD
    { const char *t = getenv("DM_TEST_SAVEPKG_IN"); if (t) { if (!strcmp(t, "cancel")) { LOG("存档管理：测试旁路：取消选文件"); return; } start_import(0, t); return; } }   // 系统选文件驱动不了，QA 构建直接给路径（CLAUDE.md §2.9）| the system picker cannot be driven, QA builds take a path directly
#endif
    if (!g_pick) { show_fail(DM_PKG_E_IO, S_HOME); return; } atomic_store(&g_pick_wait, 1); LOG("存档管理：打开系统选文件"); g_pick();
}
static void share_now(const char *path) {
#ifdef DM_QA_BUILD
    const char *t = getenv("DM_TEST_SAVEPKG_OUT"); if (t && !strcmp(t, "cancel")) { atomic_store(&g_share_done, -1); return; } if (t) { FILE *a = fopen(path, "rb"), *b = fopen(t, "wb"); int ok = a && b; char buf[65536]; size_t n; while (ok && (n = fread(buf, 1, sizeof buf, a))) ok = fwrite(buf, 1, n, b) == n; if (a) fclose(a); if (b) fclose(b); atomic_store(&g_share_done, ok ? 1 : -1); return; }
#endif
    dm_savepkg_path_set(g_share_path, sizeof g_share_path, path);   // 导出传进来的就是它自己 | an export passes this very buffer
    if (!g_share) { atomic_store(&g_share_done, -1); return; } g_share(path);
}
static int name_taken(const char *c, void *ctx) { (void)ctx; char p[1400]; struct stat st; snprintf(p, sizeof p, "%s/%s", save_dir(), c); if (!stat(p, &st)) return 1; for (int i = 0; i < g_view.nchars; i++) if (!strcasecmp(g_view.chars[i].name, c) || !strcasecmp(g_view.chars[i].newname, c)) return 1; return 0; }
static void row_action(row_t *r) {
    int st = atomic_load(&g_st);
    if (st == S_HOME) { if (r->idx == 0) export_page(); else if (r->idx == 1) { g_home_import = 1; request_pick(); } else if (r->idx == 2) job_start(J_BACKUP, NULL); else bklist_page(); return; }
    if (st == S_EXPORT) { r->on = !r->on; strlcpy(r->tag, S(r->on ? "dmSaveSelected" : "dmSaveNotSelected"), sizeof r->tag); return; }
    if (st == S_PREVIEW) {
        if (r->kind == RK_CHAR) { dm_pkg_char *c = &g_view.chars[r->idx]; if (c->status == DM_PKG_SAME || c->status == DM_PKG_BAD) return;
            if (c->status == DM_PKG_NEW) c->action = c->action == DM_PKG_SKIP ? DM_PKG_OVERWRITE : DM_PKG_SKIP;
            else { int nx = c->action == DM_PKG_SKIP ? DM_PKG_OVERWRITE : c->action == DM_PKG_OVERWRITE ? (DM_SAVEMGR_KEEPBOTH ? DM_PKG_KEEPBOTH : DM_PKG_SKIP) : DM_PKG_SKIP; c->newname[0] = 0; if (nx == DM_PKG_KEEPBOTH && !dm_savepkg_keepboth_name(c->name, c->newname, sizeof c->newname, name_taken, NULL)) nx = DM_PKG_SKIP; c->action = nx; } }
        else if (r->kind == RK_STASH) { dm_pkg_stash *x = &g_view.stash[r->idx]; if (x->status == DM_PKG_SAME) return; x->action = x->action == DM_PKG_OVERWRITE ? DM_PKG_SKIP : DM_PKG_OVERWRITE; }
        else if (r->kind == RK_SETTINGS) g_view.settings_action = g_view.settings_action == DM_PKG_OVERWRITE ? DM_PKG_SKIP : DM_PKG_OVERWRITE;
        preview_tag(r); return; }
    if (st == S_BKLIST) { g_bk_sel = r->idx; bkitem_page(); return; }
    if (st == S_BKITEM) { dm_pkg_backup *b = &g_bk[g_bk_sel]; char p[1500]; snprintf(p, sizeof p, "%s/%s", b->dir, b->file);
        if (r->idx == 0) start_import(1, p); else if (r->idx == 1) { int rc = dm_savepkg_backup_star(save_dir(), b, !b->starred); if (rc) { show_fail(rc, S_BKLIST); return; } b->starred = !b->starred; bklist_page(); } else if (r->idx == 2) { go(S_WORK); set_box(S("dmSaveWorking"), S("dmSaveWorkingSub"), NULL, NULL, NULL); share_now(p); } else { rows_clear(); set_box(S("dmSaveBkDeleteAsk"), S("dmSaveBkDeleteSub"), NULL, S("dmSaveBkDelete"), S("dmSaveBack")); go(S_BKDEL); } return; }
}
static void *shutdown_main(void *a) { (void)a; dm_host_shutdown("save import"); return NULL; }
static void close_box(void) { go(S_NONE); if (g_view_ok) { dm_savepkg_view_free(&g_view); g_view_ok = 0; } }
static void btn_action(int k) {
    int st = atomic_load(&g_st);
    switch (st) {
    case S_HOME: close_box(); LOG("存档管理：收起"); break;
    case S_EXPORT: if (k == 1) { show_home(); break; } { static const char *names[MAX_ROWS]; static char store[DM_PKG_MAX_CHARS][64]; int n = 0; g_sel_stash = g_sel_settings = 0;
            for (int i = 0; i < g_nrows; i++) { row_t *r = &g_rows[i]; if (r->kind == RK_STASH) g_sel_stash = r->on; else if (r->kind == RK_SETTINGS) g_sel_settings = r->on; else if (r->kind == RK_CHAR && r->on && n < DM_PKG_MAX_CHARS) { const char *sep = strstr(r->text, " · "); size_t L = sep ? (size_t)(sep - r->text) : strlen(r->text); snprintf(store[n], sizeof store[n], "%.*s", (int)L, r->text); names[n] = store[n]; n++; } } g_sel_names = names; g_sel_n = n; job_start(J_EXPORT, NULL); } break;
    case S_PREVIEW: if (k == 1) { dm_savepkg_stage_cancel(save_dir()); dm_savepkg_view_free(&g_view); g_view_ok = 0; show_home(); break; }
        { int act = g_view.settings_action == DM_PKG_OVERWRITE; for (int i = 0; i < g_view.nchars; i++) act |= g_view.chars[i].action != DM_PKG_SKIP && g_view.chars[i].status != DM_PKG_BAD && g_view.chars[i].status != DM_PKG_SAME; for (int i = 0; i < g_view.nstash; i++) act |= g_view.stash[i].action == DM_PKG_OVERWRITE; if (!act) { dm_savepkg_stage_cancel(save_dir()); dm_savepkg_view_free(&g_view); g_view_ok = 0; show_home(); LOG("存档管理：没有要导入的内容，回首页"); break; }
          int rc = dm_savepkg_stage_commit(save_dir(), &g_view, NULL); dm_savepkg_view_free(&g_view); g_view_ok = 0; if (rc) { show_fail(rc, S_HOME); break; } rows_clear(); set_box(S("dmSaveRestartTitle"), S("dmSaveRestartSub"), NULL, S("dmSaveOk"), NULL); go(S_RESTART); } break;
    case S_RESTART: { LOG("存档管理：确认退出，下次启动换入"); pthread_t th; if (pthread_create(&th, NULL, shutdown_main, NULL)) dm_host_shutdown("save import"); else pthread_detach(th); atomic_store(&g_shown, 2); } break;
    case S_MSG: if (g_msg_back == S_HOME) show_home(); else if (g_msg_back == S_BKLIST) bklist_page(); else close_box(); break;
    case S_BKLIST: show_home(); break;
    case S_BKITEM: bklist_page(); break;
    case S_BKDEL: if (k == 0) { dm_savepkg_backup_delete(save_dir(), &g_bk[g_bk_sel]); LOG("存档管理：删除一份备份"); bklist_page(); } else bkitem_page(); break;
    default: break; }
}
void dm_savemgr_open(int from) { if (dm_uiscreen_ingame()) { LOG("存档管理：局内不打开（入口 %d）", from); return; } g_home_import = 0; { const char *sv = save_dir(); if (sv && dm_savepkg_is_coop_root(sv)) { show_msg(S("dmSaveMgrRow"), S("dmSaveCoopRoot"), S_NONE); LOG("存档管理：联机存档根，不开面板（入口 %d）", from); return; } } if (from == 3) { rows_clear(); set_box(S("dmSaveWorking"), S("dmSaveWorkingSub"), NULL, NULL, NULL); go(S_WORK); request_pick(); LOG("存档管理：打开（入口 3）"); return; } show_home(); LOG("存档管理：打开（入口 %d）", from); }
void dm_savemgr_on_uimsg(const char *cat, const char *act, const char *arg) { if (!cat || !act || !arg || strcmp(cat, "PanelManager") || strcmp(act, "ClosePanel")) return; if (!strcmp(arg, "TouchSavesOpt")) dm_savemgr_open(1); else if (!strcmp(arg, "TouchSavesCc")) dm_savemgr_open(3); }

// ---- 弹窗：输入 | The box: input
static _Atomic uint64_t g_md_ev; static _Atomic int g_after;
static int stick_off(int32_t v) { return v > -8000 && v < 8000; }
void dm_savemgr_input_gate(uint64_t *bt, int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry) {
    static uint64_t prev, pdir, held; static int stick; static dm_stick_nav nav;
    if (!bt || !lx || !ly || !rx || !ry) return;
    if (!dm_savemgr_modal()) { if (atomic_load(&g_after)) { int centred = stick_off(*lx) && stick_off(*ly) && stick_off(*rx) && stick_off(*ry); held &= *bt; if (centred) stick = 0; if (held || stick) { *bt = 0; *lx = *ly = *rx = *ry = 0; } else atomic_store(&g_after, 0); } prev = *bt; pdir = 0; dm_stick_nav_reset(&nav); return; }
    held = *bt; stick = !(stick_off(*lx) && stick_off(*ly) && stick_off(*rx) && stick_off(*ry)); atomic_store(&g_after, 1);
    uint64_t down = *bt & ~prev; prev = *bt; int32_t vy = (*ly < 0 ? -*ly : *ly) >= (*ry < 0 ? -*ry : *ry) ? *ly : *ry; uint64_t dir = dm_stick_nav_dirs(&nav, *lx, vy) & (DM_BTN_UP | DM_BTN_DOWN);
    uint64_t ev = down & (DM_BTN_A | DM_BTN_B | DM_BTN_UP | DM_BTN_DOWN | DM_BTN_L | DM_BTN_R); if (dir & ~pdir) ev |= dir; pdir = dir; if (ev) atomic_fetch_or(&g_md_ev, ev); *bt = 0; *lx = *ly = *rx = *ry = 0;
}
typedef struct { int valid, vh, ox, oy, nrows, x0, x1, row_y0, btn_y, btn_x[2], nbtn; float s; } tapgeo; static tapgeo g_tap;
#define ROW_H 88
#define BTN_W 470
#define BTN_H 115
void dm_savemgr_ui_tap(uint64_t root, int x, int y) {
    (void)root; const tapgeo *g = &g_tap; if (!g->valid || g->vh <= 0 || !(g->s > 0.05f) || atomic_load(&g_shown) != 1) return;
    float u = (float)x * 2160 / g->vh, v = (float)y * 2160 / g->vh, lx = (u - g->ox) / g->s, ly = (v - g->oy) / g->s;
    for (int k = 0; k < g->nbtn; k++) if (lx >= g->btn_x[k] - 40 && lx <= g->btn_x[k] + BTN_W + 40 && ly >= g->btn_y - 30 && ly <= g->btn_y + BTN_H + 30) { LOG("存档管理：点按钮 %d", k); btn_action(k); return; }
    if (lx >= g->x0 && lx <= g->x1 && ly >= g->row_y0 && ly < g->row_y0 + g->nrows * ROW_H) { int r = (int)((ly - g->row_y0) / ROW_H), i = g_page * PER_PAGE + r; if (i < g_nrows) { LOG("存档管理：点第 %d 行", r); row_action(&g_rows[i]); } return; }
    if (g_nrows > PER_PAGE && ly >= g->row_y0 + g->nrows * ROW_H && ly < g->btn_y - 30) { int pages = (g_nrows + PER_PAGE - 1) / PER_PAGE; if (lx > g->x0 + (g->x1 - g->x0) / 2) { if (g_page + 1 < pages) g_page++; } else if (g_page > 0) g_page--; g_foc = 0; }   // 页码那一条：点右半下一页、左半上一页 | the page strip: right half next, left half previous
}

// ---- 弹窗：画 | The box: drawing
static const char *const k_save_hosts[] = { "VideoOptionsPanel", "CharacterCreatePanel", "MainMenuButtonRibbon", "MainMenuPanel", "PauseLayout", "PauseLayoutGarden" };
static uint64_t box_host(uint64_t root) { for (size_t i = 0; i < sizeof k_save_hosts / sizeof *k_save_hosts; i++) { uint64_t p = dm_ui_top(root, k_save_hosts[i]); if (p && *(uint8_t *)(p + 0x50) && co_find(p, "TouchSaveTitle", 0)) return p; } return 0; }
static void hide_all(uint8_t *b, uint64_t p) {
    static const char *const nm[] = { "TouchSaveTitle", "TouchSaveSub", "TouchSavePage", "TouchSaveNote", "TouchSaveBtn0", "TouchSaveBtn1", "TouchSaveBtnText0", "TouchSaveBtnText1" }; for (size_t i = 0; i < sizeof nm / sizeof *nm; i++) { uint64_t w = co_find(p, nm[i], 0); if (w) { co_place(w, 99999, 99999); if (strncmp(nm[i], "TouchSaveBtn", 12) || strstr(nm[i], "Text")) co_text(b, w, ""); } }
    for (int k = 0; k < PER_PAGE; k++) { char n[32]; snprintf(n, sizeof n, "TouchSaveRow%d", k); uint64_t w = co_find(p, n, 0); if (w) { co_place(w, 99999, 99999); co_text(b, w, ""); } snprintf(n, sizeof n, "TouchSaveTag%d", k); w = co_find(p, n, 0); if (w) { co_place(w, 99999, 99999); co_text(b, w, ""); } }
    for (int k = 0; k < 15; k++) { char n[24]; snprintf(n, sizeof n, "TouchSaveTile%d%d", k / 3, k % 3); uint64_t w = co_find(p, n, 0); if (w) { int32_t *r = (int32_t *)(w + 0x70); r[0] = r[1] = 99999; } } co_rect_hide(p, "TouchSaveFocus");
}
static void entries(uint8_t *b, uint64_t root) {   // 两个入口的显隐：自定义页这一行、建角页的按钮 | the two entries: the Custom page row, the class-select button
    int ing = dm_uiscreen_ingame(), pad = dm_padmode_current(); uint64_t vp = dm_ui_top(root, "VideoOptionsPanel");
    if (vp && *(uint8_t *)(vp + 0x50)) { uint64_t bt = co_find(vp, "TouchSavesOpt", 0), hi = co_find(vp, "TouchSavesHint", 0); if (bt && hi && (((int32_t *)(bt + 0x70))[0] == 99999) != ing) { co_place(bt, ing ? 99999 : 1110, 30); co_place(hi, ing ? 680 : 99999, 0); co_text(b, hi, ing ? S("dmSaveInRun") : ""); } }   // 面板可能重建，按控件现状判，不缓存 | the panel may be rebuilt: judge by the widget, no caching
    uint64_t cc = dm_ui_top(root, "CharacterCreatePanel"); static int cc_was, none; if (!(cc && *(uint8_t *)(cc + 0x50))) cc_was = 0; else { uint64_t t = co_find(cc, "TouchSavesCc", 0), tp = co_find(cc, "TouchSavesCcPad", 0); const char *sv = save_dir(); if (!cc_was) { cc_was = 1; none = 0; if (sv) { DIR *d = opendir(sv); struct dirent *e; none = 1; while (none && d && (e = readdir(d))) { if (e->d_name[0] == '.' || e->d_name[0] == '_') continue; char p[1400]; struct stat st; snprintf(p, sizeof p, "%s/%s/%s.d2s", sv, e->d_name, e->d_name); if (!stat(p, &st)) none = 0; } if (d) closedir(d); } }   // 只在这页出现那一刻看一次盘 | look at the disk only when the page appears
        int show = none && !ing && !dm_savemgr_modal();
        if (t) co_place(t, show && !pad ? 275 : 99999, 1900); if (tp) co_place(tp, show && pad ? 420 : 99999, 1900);   // 手柄版让开中间的键位提示条 | the controller version clears the legend in the middle
        if (show && pad && (dm_padmode_page_keys(DM_BTN_Y) & DM_BTN_Y)) dm_savemgr_open(3); }
}
#ifdef DM_QA_BUILD
// QA 构建的脚本驱动（CLAUDE.md §2.9，玩家包不编译）：DM_TEST_UI=<秒>:<identifier>:tap[,…]。秒从第一次 pump 算；排队执行，前一项做完、控件在屏上才做下一项。identifier 表见设计稿「自动化驱动」
// Scripted driver for QA builds (CLAUDE.md §2.9, not compiled into player packages): DM_TEST_UI=<seconds>:<identifier>:tap[,...]. Seconds count from the first pump; items run in order, each waits for the previous one and for its control to be on screen. Identifier table: design doc, "Automation"
static void qa_pump(uint64_t root) {
    static const char *e; static int init; static uint64_t t0; struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); uint64_t now = (uint64_t)ts.tv_sec;
    if (!init) { init = 1; e = getenv("DM_TEST_UI"); t0 = now; } if (!e || !*e) return; char id[48]; int sec = 0, n = 0; if (sscanf(e, "%d:%47[^:]:tap%n", &sec, id, &n) != 2 || !n) { LOG("【红】DM_TEST_UI 看不懂：%.40s", e); e = NULL; return; }
    if (now - t0 < (uint64_t)sec) return; int k = -1, st = atomic_load(&g_st), done = 0;
    if (!strcmp(id, "TouchSavesOpt") || !strcmp(id, "TouchSavesCc")) { if (st == S_NONE && !dm_uiscreen_ingame() && box_host(root)) { dm_savemgr_open(id[10] == 'O' ? 2 : 3); done = 1; } }
    else if (atomic_load(&g_shown) == 1 && st != S_WORK) {
        if (sscanf(id, "TouchSaveRow%d", &k) == 1 && k >= 0 && g_page * PER_PAGE + k < g_nrows) { row_action(&g_rows[g_page * PER_PAGE + k]); done = 1; }
        else if (sscanf(id, "TouchSaveBtn%d", &k) == 1 && k >= 0 && k < g_nbtn) { btn_action(k); done = 1; } }
    if (done) { LOG("存档管理：DM_TEST_UI %s", id); e += n; if (*e == ',') e++; }
}
#endif

void dm_savemgr_pump(void) {
    uint8_t *b = base_(); if (!b) return; uint64_t root = *(uint64_t *)(b + 0x2f05458); if (!root) return;
    entries(b, root);
    // 启动后第一次到前端：换入结果 | first time at the front end after boot: the apply result
    { static int checked; const char *sv = save_dir(); if (!checked && sv && !dm_uiscreen_ingame() && box_host(root) && !dm_savemgr_modal()) { checked = 1; int ok = 0, ch = 0, code = 0; if (dm_savepkg_result_take(sv, &ok, &ch, &code)) { LOG("存档管理：换入结果 %s（%d 个角色）", ok ? "成功" : "失败", ch); if (ok) { char t[160]; snprintf(t, sizeof t, S("dmSaveResultOk"), ch); show_msg(t, S("dmSaveResultOkSub"), S_NONE); } else { show_msg(S("dmSaveResultFail"), S("dmSaveFailSub"), S_NONE); } } } }
    int st = atomic_load(&g_st);
#ifdef DM_QA_BUILD
    qa_pump(root);
#endif
    // 外壳回调与后台活的结果，都在这里接（游戏主线程）| shell callbacks and job results are taken here (game main thread)
    if (atomic_exchange(&g_picked_new, 0)) { char p[1400]; pthread_mutex_lock(&g_mu); strlcpy(p, g_picked, sizeof p); pthread_mutex_unlock(&g_mu); atomic_store(&g_pick_wait, 0); if (p[0]) start_import(0, p); else { LOG("存档管理：取消选文件"); if (g_home_import) show_home(); else close_box(); } st = atomic_load(&g_st); }
    { int sd = atomic_exchange(&g_share_done, 0); if (sd) { char d[1400]; snprintf(d, sizeof d, "%s.export.tmp", save_dir()); dm_savepkg_rmtree(d); LOG("存档管理：系统面板关闭（%s）", sd > 0 ? "已保存" : "没保存"); if (st == S_WORK) show_msg(S(sd > 0 ? "dmSaveExportDone" : "dmSaveExportCancel"), sd > 0 ? S("dmSaveExportDoneSub") : "", S_HOME); st = atomic_load(&g_st); } }
    if (st == S_WORK && atomic_exchange(&g_job_done, 0)) { int rc = atomic_load(&g_job_rc), j = g_job; g_job = J_NONE;
        if (j == J_INSPECT) { if (rc) show_fail(rc, g_home_import || g_restore ? S_HOME : S_NONE); else preview_page(); }
        else if (j == J_BACKUP) { if (rc) show_fail(rc, S_HOME); else show_msg(S("dmSaveBackupDone"), S("dmSaveBkKeep"), S_HOME); }
        else if (j == J_EXPORT) { if (rc) show_fail(rc, S_HOME); else share_now(g_share_path); }
        st = atomic_load(&g_st); }
    // 底下菜单的焦点：同社区框，开着时清空、关掉交还 | the focus of the menu underneath: as the community box, cleared while open and handed back on close
    int pad = dm_padmode_current();
    { static uint64_t saved; static char saved_nm[64]; static int held; uint64_t fm = *(uint64_t *)(root + 0xd0);
      if (st && fm && pad) { uint64_t F = *(uint64_t *)(fm + 0x190); if (!held) { held = 1; saved = F; const char *nm = F ? *(const char **)(F + 8) : NULL; snprintf(saved_nm, sizeof saved_nm, "%s", nm ? nm : ""); } if (F) *(uint64_t *)(fm + 0x190) = 0; }
      else if (held && !st) { held = 0; const char *nm = saved && co_has(root, saved, 0) ? *(const char **)(saved + 8) : NULL; if (fm && nm && !strcmp(nm, saved_nm)) *(uint64_t *)(fm + 0x190) = saved; saved = 0; } }
    static uint64_t last_host; if (!st) { if (last_host && co_has(root, last_host, 0)) hide_all(b, last_host); last_host = 0; g_tap.valid = 0; return; }   // 只在收起那一刻写一次，平时不碰控件 | written once on closing; widgets are left alone otherwise
    uint64_t p = box_host(root); if (!p) { g_tap.valid = 0; return; } if (last_host && last_host != p && co_has(root, last_host, 0)) hide_all(b, last_host); last_host = p;
    // 手柄事件 | controller events
    { uint64_t ev = atomic_exchange(&g_md_ev, 0); if (!pad) ev = 0; int shown = atomic_load(&g_shown) == 1; if (ev && shown) { int pages = g_nrows ? (g_nrows + PER_PAGE - 1) / PER_PAGE : 1, on = g_nrows - g_page * PER_PAGE; if (on > PER_PAGE) on = PER_PAGE; if (on < 0) on = 0; int total = on + g_nbtn;
          if ((ev & DM_BTN_UP) && g_foc > 0) g_foc--; if ((ev & DM_BTN_DOWN) && g_foc + 1 < total) g_foc++;
          if ((ev & DM_BTN_L) && g_page > 0) { g_page--; g_foc = 0; } if ((ev & DM_BTN_R) && g_page + 1 < pages) { g_page++; g_foc = 0; }
          if (ev & DM_BTN_A) { if (g_foc < on) row_action(&g_rows[g_page * PER_PAGE + g_foc]); else if (g_foc - on < g_nbtn) btn_action(g_foc - on); }
          else if (ev & DM_BTN_B) { if (g_nbtn && st != S_WORK && st != S_RESTART) btn_action(g_nbtn - 1); }
          st = atomic_load(&g_st); if (!st) return; } }
    int vw = *(int32_t *)(b + 0x211da38), vh = *(int32_t *)(b + 0x211da3c), xr = vh > 0 ? (int)((long)vw * 2160 / vh) : 3840; int ox, oy; float s; co_origin(p, xr, &ox, &oy, &s);
    int pages = g_nrows ? (g_nrows + PER_PAGE - 1) / PER_PAGE : 1; if (g_page >= pages) g_page = pages - 1; int on = g_nrows - g_page * PER_PAGE; if (on > PER_PAGE) on = PER_PAGE; if (on < 0) on = 0;
    int has_note = g_note[0] != 0, row_y0 = 180, y_after = row_y0 + on * ROW_H + (on ? 10 : 0), page_y = y_after, note_y = page_y + (pages > 1 ? 50 : 0), btn_y = note_y + (has_note ? 60 : 0) + 20, H = btn_y + (g_nbtn ? BTN_H : 0) + 45;
    int cx = (int)((xr / 2 - ox) / s), y0 = (int)((1080 - oy) / s) - H / 2;
#define W(nm) co_find(p, nm, 0)
    if (!W("TouchSaveTitle") || !W("TouchSaveBtn1") || !W("TouchSaveRow5")) { go(S_NONE); LOG("【红】存档管理：面板里的控件不全，收起"); return; }
    { uint64_t tl[15]; for (int k = 0; k < 15; k++) { char nm[24]; snprintf(nm, sizeof nm, "TouchSaveTile%d%d", k / 3, k % 3); tl[k] = W(nm); } int gm[15][4], h = dm_notice_geometry(BOX_W - 128, H - 24, BOX_W, gm), top = (244 - h) / 2; for (int k = 0; h && k < 15; k++) { int r[4] = { cx - BOX_W / 2 + gm[k][0], y0 + gm[k][1] - top, gm[k][2], gm[k][3] + (k / 3 < 4 ? 2 : 0) }; if (tl[k]) co_tile(b, tl[k], r); } }
    char line[300]; strlcpy(line, g_title, sizeof line); dm_community_fit(line, sizeof line, 48, 1700); co_text(b, W("TouchSaveTitle"), line); co_place(W("TouchSaveTitle"), cx - 850, y0 + 40);
    strlcpy(line, g_sub, sizeof line); dm_community_fit(line, sizeof line, 28, 1700); co_text(b, W("TouchSaveSub"), line); co_place(W("TouchSaveSub"), cx - 850, y0 + 105);
    for (int k = 0; k < PER_PAGE; k++) { char nm[32]; snprintf(nm, sizeof nm, "TouchSaveRow%d", k); uint64_t r = W(nm); snprintf(nm, sizeof nm, "TouchSaveTag%d", k); uint64_t t = W(nm); if (k >= on) { co_place(r, 99999, 99999); co_text(b, r, ""); co_place(t, 99999, 99999); co_text(b, t, ""); continue; } row_t *x = &g_rows[g_page * PER_PAGE + k]; int ry = y0 + row_y0 + k * ROW_H; strlcpy(line, x->text, sizeof line); dm_community_fit(line, sizeof line, 36, 1120); co_text(b, r, line); co_place(r, cx - 840, ry + 6); strlcpy(line, x->tag, sizeof line); dm_community_fit(line, sizeof line, 28, 500); co_text(b, t, line); co_place(t, cx + 340, ry + 6); }
    if (pages > 1) { char pg[32]; snprintf(pg, sizeof pg, "%d / %d", g_page + 1, pages); co_text(b, W("TouchSavePage"), pg); co_place(W("TouchSavePage"), cx - 850, y0 + page_y); } else { co_text(b, W("TouchSavePage"), ""); co_place(W("TouchSavePage"), 99999, 99999); }
    if (has_note) { strlcpy(line, g_note, sizeof line); dm_community_fit(line, sizeof line, 28, 1700); co_text(b, W("TouchSaveNote"), line); co_place(W("TouchSaveNote"), cx - 850, y0 + note_y); } else { co_text(b, W("TouchSaveNote"), ""); co_place(W("TouchSaveNote"), 99999, 99999); }
    tapgeo g = { 1, vh, ox, oy, on, cx - 860, cx + 860, y0 + row_y0, y0 + btn_y, { 0, 0 }, g_nbtn, s };
    for (int k = 0; k < 2; k++) { char nm[32], nt[32]; snprintf(nm, sizeof nm, "TouchSaveBtn%d", k); snprintf(nt, sizeof nt, "TouchSaveBtnText%d", k); if (k >= g_nbtn) { co_place(W(nm), 99999, 99999); co_place(W(nt), 99999, 99999); co_text(b, W(nt), ""); continue; } int bx = g_nbtn == 1 ? cx - BTN_W / 2 : k == 0 ? cx + 20 : cx - BTN_W - 20; g.btn_x[k] = bx; co_place(W(nm), bx, y0 + btn_y); co_place(W(nt), bx, y0 + btn_y + 2); strlcpy(line, g_btn[k], sizeof line); dm_community_fit(line, sizeof line, 36, BTN_W - 40); co_text(b, W(nt), line); }
    g_tap = g;
    if (pad && (on || g_nbtn)) { if (g_foc >= on + g_nbtn) g_foc = on + g_nbtn - 1; if (g_foc < on) co_rect_lines(b, p, "TouchSaveFocus", "focus_h", "focus_v", cx - 860, y0 + row_y0 + g_foc * ROW_H, 1720, ROW_H, 4); else { int k = g_foc - on; co_rect_lines(b, p, "TouchSaveFocus", "focus_h", "focus_v", g.btn_x[k] - 8, y0 + btn_y - 8, BTN_W + 16, BTN_H + 16, 4); } } else co_rect_hide(p, "TouchSaveFocus");
#undef W
    if (atomic_load(&g_shown) == 0) { atomic_store(&g_shown, 1); LOG("存档管理：已显示 页 %d（%d 行，框高 %d）", st, g_nrows, H); }
}
