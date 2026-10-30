// 存档管理第一期的公共核心（格式与流程见 dm_savepkg.c 文件头）：导出 ZIP、校验与预览、待应用目录、启动期换入、备份列表。不碰游戏内存，不碰界面。
// Shared core of save management, phase 1 (format and flow in the header of dm_savepkg.c): ZIP export, validation and preview, the pending directory, apply-at-boot, the backup list. Touches neither game memory nor UI.
#pragma once
#include <stddef.h>
#include <stdint.h>

enum { DM_PKG_OK = 0, DM_PKG_E_IO = 1, DM_PKG_E_ZIP = 2, DM_PKG_E_NOTOURS = 3, DM_PKG_E_CHECK = 4, DM_PKG_E_TOONEW = 5, DM_PKG_E_LIMIT = 6, DM_PKG_E_PATH = 7, DM_PKG_E_SPACE = 8, DM_PKG_E_STARFULL = 9, DM_PKG_E_CONVERT = 10, DM_PKG_E_COOP = 11 };
enum { DM_PKG_NEW = 0, DM_PKG_SAME = 1, DM_PKG_CONFLICT = 2, DM_PKG_BAD = 3 };           // 预览里每行的状态 | row state in the preview
enum { DM_PKG_SKIP = 0, DM_PKG_OVERWRITE = 1, DM_PKG_KEEPBOTH = 2 };                      // 每行的处理（新角色导入 = OVERWRITE） | per-row action (importing a new character = OVERWRITE)
enum { DM_PKG_FORMAT_MAJOR = 1, DM_PKG_FORMAT_MINOR = 0, DM_PKG_SAVE_GEN = 2 };           // 版本规则见设计稿 2.1 节：旧版跳过会丢东西的改动必须升 major | versioning rules: a change an old build would lose data by skipping must bump major
#define DM_PKG_MAX_CHARS 256
#define DM_PKG_MAX_STASH 8

typedef struct { const char *platform, *commit, *build, *version, *game_build; } dm_pkg_source;   // 来源端：只显示，不参与判断；不放任何设备标识 | origin: display only, never decides anything; no device identifiers
typedef struct {                                                                          // 导出范围 | export scope
    const char *const *names; int nnames;   // nnames < 0 = 全部角色 | nnames < 0 = all characters
    int stash, settings;                    // 含共享仓库 / 含设置（设置 = Settings/ + 白名单键） | include shared stash / settings (Settings/ + the allow-listed keys)
    const char *custom_dir;                 // sw_custom.txt 所在目录，可空 | directory of sw_custom.txt, may be NULL
    const char *kind;                       // "export" / "manual" / "auto" | "export" / "manual" / "auto"
    int64_t now;                            // 秒（UTC），写进 manifest | seconds (UTC), written into the manifest
    int (*progress)(int done, int total);   // 可空；返回非 0 中止 | may be NULL; non-zero aborts
} dm_pkg_export_opts;

typedef struct {
    char name[64], local[64], newname[64];
    int cls, level, progress, hardcore, expansion, elysium, topup;
    uint32_t last_played;
    int status, action;                     // DM_PKG_NEW… / DM_PKG_SKIP… | DM_PKG_NEW… / DM_PKG_SKIP…
    int local_level; uint32_t local_played; // 同名时本机那份 | this device's copy when the names clash
} dm_pkg_char;
typedef struct { char name[64]; int status, action; } dm_pkg_stash;
typedef struct {
    int format_major, format_minor, save_gen;
    char platform[16], commit[48], build[24], version[40], game_build[24], exported_at[24], kind[12];
    char source_format[20], source_version[24], converted_from[48];   // 数据来自哪种格式 / 那种格式的版本 / 谁转换过（空 = 没转换）。第一期只有恒等转换 | which format the data came from / its version / what converted it (empty = not converted). Phase 1 only has the identity conversion
    int nchars, nstash, has_settings, settings_action, skipped_unknown, nfiles;
    long long total_bytes;
    dm_pkg_char *chars;                     // DM_PKG_MAX_CHARS 个，dm_savepkg_view_free 释放 | DM_PKG_MAX_CHARS entries, released by dm_savepkg_view_free
    dm_pkg_stash stash[DM_PKG_MAX_STASH];
} dm_pkg_view;

typedef struct { char file[96]; char dir[1024]; long long size; int64_t when; int chars, starred, is_auto, mirror; } dm_pkg_backup;

int dm_savepkg_name_ok(const char *name);   // 角色名规则（hle_charname.c）：2–15 字节、首尾字母（≥0x80 算字母）、- _ 合计最多一个、合法 UTF-8 | character name rule
int dm_savepkg_path_class(const char *rel, char *unit, size_t un);   // 路径白名单：-1 不安全（整包拒绝）/ 0 不认识（跳过）/ 1 角色 / 2 共享仓库 / 3 设置 | path allow-list: -1 unsafe (reject) / 0 unknown (skip) / 1 character / 2 stash / 3 settings
int dm_savepkg_keepboth_name(const char *name, char *out, size_t n, int (*taken)(const char *candidate, void *ctx), void *ctx);   // 「保留两份」取新名，全部被占用返回 0 | new name for keep-both; 0 when all are taken
int dm_savepkg_parse_d2s(const uint8_t *b, size_t n, dm_pkg_char *out);   // 头部校验 + 字段；1 = 有效 | header validation + fields; 1 = valid
int dm_savepkg_rename_d2s(uint8_t *b, size_t n, const char *newname);     // 改名字段并重算校验和；1 = 成功 | patch the name and recompute the checksum; 1 = done

int dm_savepkg_export(const char *tree, const char *out_zip, const dm_pkg_export_opts *o, const dm_pkg_source *src);   // tree = 不会再变的目录（快照或挂载之前的已提交目录） | tree = a directory that will not change (a snapshot, or the committed directory before mount)
int dm_savepkg_inspect(const char *zip, const char *save, int restore, dm_pkg_view *v);   // 校验并解到 <save>.import.tmp，不碰存档；restore = 从备份恢复（缺省处理改成覆盖） | validates and extracts into <save>.import.tmp; the save is untouched; restore = from a backup (actions default to overwrite)
int dm_savepkg_stage_commit(const char *save, const dm_pkg_view *v, const char *const *custom_kv);   // 确认：写 plan.tsv、原子改名成 <save>.import | confirm: write plan.tsv and atomically rename to <save>.import
void dm_savepkg_stage_cancel(const char *save);
void dm_savepkg_view_free(dm_pkg_view *v);
int dm_savepkg_apply_pending(const char *save, const char *custom_dir, int64_t now, const dm_pkg_source *src);   // 启动期、挂存档与云同步之前调：0 = 没有待应用，1 = 换入成功，-1 = 失败（存档不变） | at boot, before mount and cloud sync: 0 = nothing pending, 1 = applied, -1 = failed (save unchanged)
int dm_savepkg_result_take(const char *save, int *ok, int *chars, int *code);   // 取上次换入的结果（读完删）；1 = 有 | take the last apply's result (deleted once read); 1 = present

int dm_savepkg_mirror_from_downloads(const char *downloads);   // 安卓：按「Plus 优先、旧名其次、都没有建 Plus」定写入目录，另一个只读；0 = 成功 | Android: picks the write folder (Plus, else the old name, else create Plus), the other is read only; 0 = ok
void dm_savepkg_set_mirror(const char *dir);    // 安卓：备份另存一份的目录（卸载后还在）；空 = 不另存 | Android: second backup directory that survives uninstall; NULL = none
int dm_savepkg_backup(const char *save, const char *kind, int64_t now, const char *custom_dir, const dm_pkg_source *src, char *name_out, size_t n);
int dm_savepkg_backup_tree(const char *save, const char *tree, const char *kind, int64_t now, const char *custom_dir, const dm_pkg_source *src, char *name_out, size_t n);   // tree = 快照（运行中用：打包时不怕提交）| tree = a snapshot (while running: commits cannot interfere)
int dm_savepkg_backup_list(const char *save, dm_pkg_backup *out, int cap);       // 新的在前 | newest first
int dm_savepkg_backup_star(const char *save, const dm_pkg_backup *b, int on);
int dm_savepkg_backup_delete(const char *save, const dm_pkg_backup *b);
void dm_savepkg_backup_prune(const char *save, const char *keep);                // 未标星只留最近 10 份，每个目录各算（keep 这份不删） | keep the 10 newest unstarred per directory (never deletes keep)
long long dm_savepkg_free_bytes(const char *path);
int dm_savepkg_rmtree(const char *path);
int dm_savepkg_is_coop_char(const char *root, const char *phys);   // 这个角色目录有没有 .sw_coop 标记（约定见 dm_savepkg.c）| whether this character directory carries a .sw_coop marker (convention in dm_savepkg.c)
int dm_savepkg_coop_count(const char *save);   // 存档根和兄弟目录 <存档根>.coop 里的联机角色数 | co-op characters in the save root and its sibling <save root>.coop
