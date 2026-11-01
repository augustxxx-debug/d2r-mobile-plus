// 存档管理第一期的公共核心（设计稿 RC2_存档管理第一期，iOS / 安卓 / Mac 同一份）。
// 包 = 一个 ZIP：第一条 manifest.json（格式版本、来源、角色列表、每个文件的相对路径 / 大小 / 修改时间 / SHA-256），后面是原文件（只写 stored，读 stored + deflate）。
// 单位 = 云同步的单位：每个角色一个（<名>/ 下全部文件）、每类共享仓库一个（ModernSharedStash* / SharedStash* 目录与根目录同名文件）；设置 = Settings/ + 白名单键。
// 导出：tree 是不会再变的目录（持存档锁克隆的快照）。只导出认得的形状，所以 _cloudinbox、iCloud 冲突副本（「名字 2.d2s」）、日志都不会进包。
// 导入两段：① 校验并解到 <save>.import.tmp，预览；② 确认后写 plan.tsv，原子改名成 <save>.import。游戏退出。
// 换入（dm_savepkg_apply_pending）在下次启动、挂存档与云同步之前：自动备份 → 克隆已提交目录为 <save>.imp → 按计划改 → 校验 → 写 _import_done → RENAME_SWAP → 收尾。
//   任一时刻被杀：互换之前已提交目录没动，下次重来；互换之后第一步认出 _import_done，只补收尾。超过两次失败就放弃（改名 .import.failed），不进启动循环。
// 只合并不删除：本机有、包里没有的角色不动。覆盖的单位文件修改时间设成换入时刻，并记进 <save>.cloudstate.forceup，云同步见到就以本地为准（dm_cloudsync.c）。
// 不信任包：manifest 字段要和实际解析的 .d2s 对得上；路径走白名单（dm_savepkg_path_class）；解压按声明大小封顶；不执行任何内容。SHA-256 只防意外损坏，不防篡改。
// Shared core of save management, phase 1 (the same code on iOS / Android / Mac).
// Package = one ZIP: first entry manifest.json (format version, origin, characters, each file's relative path / size / mtime / SHA-256), then the original files (written stored; read stored + deflate).
// Units = the cloud-sync units: one per character (all files under <name>/), one per shared-stash kind (ModernSharedStash* / SharedStash* directories and same-named root files); settings = Settings/ + allow-listed keys.
// Export: tree is a directory that will not change (a snapshot cloned under the save lock). Only known shapes are exported, so _cloudinbox, iCloud conflict copies ("name 2.d2s") and logs never enter a package.
// Import has two stages: (1) validate and extract into <save>.import.tmp, preview; (2) on confirm write plan.tsv and atomically rename to <save>.import. The game then quits.
// Apply (dm_savepkg_apply_pending) runs at the next boot, before mount and cloud sync: auto backup -> clone the committed directory to <save>.imp -> apply the plan -> verify -> write _import_done -> RENAME_SWAP -> finish up.
//   Killed at any moment: before the swap the committed directory is untouched and the next boot starts over; after the swap step 1 recognises _import_done and only finishes up. After two failures it gives up (renamed .import.failed) and never boot-loops.
// Merge only, never delete: characters present here and absent from the package stay. Overwritten units get mtime = apply time and are listed in <save>.cloudstate.forceup so cloud sync treats local as authoritative (dm_cloudsync.c).
// The package is not trusted: manifest fields must match the parsed .d2s; paths go through an allow-list (dm_savepkg_path_class); extraction is capped by declared sizes; nothing is executed. SHA-256 catches accidental damage, not tampering.
#include "dm_savepkg.h"
#include "dm.h"
#include "../third_party/libdeflate/libdeflate.h"
#include <CommonCrypto/CommonDigest.h>
#include <dirent.h>
#include <stdatomic.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/clonefile.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/xattr.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#define PKG_GUEST_GET(p, b, n) getxattr(p, "org.d2rmobile.guest-name", b, n, 0, XATTR_NOFOLLOW)
#else
#define PKG_GUEST_GET(p, b, n) lgetxattr(p, "user.org.d2rmobile.guest-name", b, n)
#endif

#define MAX_FILES 4096
#define MAX_ZIP_ENTRIES 8192
#define MAX_FILE (16u << 20)
#define MAX_TOTAL (256ll << 20)
#define MAX_MANIFEST (4u << 20)
#define KEEP_UNSTARRED 10
#define MAX_STARRED 20
typedef char pth[1400];

// ---- 测试注入：DM_TEST_SAVEPKG_FAILAT=<点>（这一步当失败）、DM_TEST_SAVEPKG_KILLAT=<点>（这一步被杀） | Test injection: the named step fails / the process dies at it
static int test_fail(const char *pt) { const char *e = getenv("DM_TEST_SAVEPKG_FAILAT"); return e && !strcmp(e, pt); }
static void test_kill(const char *pt) { const char *e = getenv("DM_TEST_SAVEPKG_KILLAT"); if (e && !strcmp(e, pt)) { LOG("DM_TEST_SAVEPKG_KILLAT：%s 处退出", pt); _exit(9); } }

// ---- 小工具 | Small helpers
static char *tok(char *s, const char *d) { static _Thread_local char *sv; return strtok_r(s, d, &sv); }
static void hex(const uint8_t *b, size_t n, char *o) { static const char d[] = "0123456789abcdef"; for (size_t i = 0; i < n; i++) { o[2 * i] = d[b[i] >> 4]; o[2 * i + 1] = d[b[i] & 15]; } o[2 * n] = 0; }
static void sha_hex(const void *d, size_t n, char o[65]) { uint8_t h[CC_SHA256_DIGEST_LENGTH]; CC_SHA256(d, (CC_LONG)n, h); hex(h, sizeof h, o); }
static int rm_tree(const char *p) {
    struct stat st; if (lstat(p, &st)) return errno == ENOENT ? 0 : -1;
    if (!S_ISDIR(st.st_mode)) return unlink(p);
    DIR *d = opendir(p); if (!d) return -1; struct dirent *e; int rc = 0; pth c;
    while ((e = readdir(d))) { if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue; snprintf(c, sizeof c, "%s/%s", p, e->d_name); rc |= rm_tree(c); }
    closedir(d); return rc | rmdir(p);
}
static uint8_t *read_all(const char *p, size_t cap, size_t *n) {
    int fd = open(p, O_RDONLY | O_NOFOLLOW); if (fd < 0) return NULL; struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || (size_t)st.st_size > cap) { close(fd); return NULL; }
    uint8_t *b = malloc((size_t)st.st_size + 1); size_t got = 0; if (!b) { close(fd); return NULL; }
    while (got < (size_t)st.st_size) { ssize_t r = read(fd, b + got, (size_t)st.st_size - got); if (r < 0 && errno == EINTR) continue; if (r <= 0) break; got += (size_t)r; }
    close(fd); if (got != (size_t)st.st_size) { free(b); return NULL; } b[got] = 0; *n = got; return b;
}
static int write_new(const char *p, const void *d, size_t n) {   // 只写新文件、不跟随链接 | new files only, links are not followed
    int fd = open(p, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644); if (fd < 0) return -1; size_t o = 0;
    while (o < n) { ssize_t w = write(fd, (const uint8_t *)d + o, n - o); if (w < 0 && errno == EINTR) continue; if (w <= 0) { close(fd); unlink(p); return -1; } o += (size_t)w; }
    int rc = fsync(fd); rc |= close(fd); if (rc) unlink(p); return rc;
}
static int write_replace(const char *p, const void *d, size_t n) { pth t; snprintf(t, sizeof t, "%s.tmp", p); unlink(t); if (write_new(t, d, n)) return -1; if (rename(t, p)) { unlink(t); return -1; } return 0; }
static int mkdirs_for(const char *p) { pth d; strlcpy(d, p, sizeof d); for (char *s = d + 1; (s = strchr(s, '/')); s++) { *s = 0; if (mkdir(d, 0755) && errno != EEXIST) return -1; *s = '/'; } return 0; }
int dm_savepkg_rmtree(const char *path) { return rm_tree(path); }
long long dm_savepkg_free_bytes(const char *path) { struct statvfs v; return statvfs(path, &v) ? -1 : (long long)v.f_bavail * (long long)v.f_frsize; }

// ---- 名字与路径 | Names and paths
static int utf8_ok(const char *s) {
    for (const uint8_t *p = (const uint8_t *)s; *p;) { int n = *p < 0x80 ? 0 : (*p & 0xe0) == 0xc0 ? 1 : (*p & 0xf0) == 0xe0 ? 2 : (*p & 0xf8) == 0xf0 ? 3 : -1; if (n < 0) return 0;
        if (*p < 0x20 || *p == 0x7f) return 0; if (n == 1 && *p < 0xc2) return 0; p++; for (int i = 0; i < n; i++, p++) if ((*p & 0xc0) != 0x80) return 0; }
    return 1;
}
static int letter(uint8_t c) { return ((c | 0x20) >= 'a' && (c | 0x20) <= 'z') || c >= 0x80; }
int dm_savepkg_name_ok(const char *name) {
    size_t n = strlen(name); if (n < 2 || n > 15 || !utf8_ok(name)) return 0; int sym = 0;
    for (size_t i = 0; i < n; i++) { uint8_t c = (uint8_t)name[i]; if (letter(c)) continue; if ((c == '-' || c == '_') && i && i + 1 < n && !sym++) continue; return 0; }
    return letter((uint8_t)name[0]) && letter((uint8_t)name[n - 1]);
}
static int is_stash_name(const char *s) { if (strncmp(s, "ModernSharedStash", 17) && strncmp(s, "SharedStash", 11)) return 0; for (; *s; s++) if (!((*s | 0x20) >= 'a' && (*s | 0x20) <= 'z') && !(*s >= '0' && *s <= '9')) return 0; return 1; }
static int char_ext_ok(const char *e) {
    static const char *const k[] = { "d2s", "d2s.meta", "key", "ctl", "map", "sw_kills", "swpage", "swpouch", "sw_rift", "swcharm" };
    for (size_t i = 0; i < sizeof k / sizeof *k; i++) if (!strcmp(e, k[i])) return 1;
    return e[0] == 'm' && e[1] == 'a' && e[2] >= '0' && e[2] <= '9' && !e[3];
}
static int ieq_ascii(const char *a, const char *b) { for (; *a && *b; a++, b++) { char x = *a, y = *b; if (x >= 'A' && x <= 'Z') x |= 0x20; if (y >= 'A' && y <= 'Z') y |= 0x20; if (x != y) return 0; } return !*a && !*b; }
enum { PC_UNSAFE = -1, PC_UNKNOWN = 0, PC_CHAR = 1, PC_STASH = 2, PC_SETTINGS = 3 };
// 路径白名单：unit 出单位名。UNSAFE = 不该出现在任何包里（穿越、绝对路径、隐藏、控制字符、_ 开头）→ 整包拒绝；UNKNOWN = 形状安全但不认识（新版本的旁路文件）→ 跳过并计数
// Path allow-list: unit receives the unit name. UNSAFE = must never appear in any package (traversal, absolute, hidden, control characters, leading _) -> reject the whole package; UNKNOWN = a safe shape we do not recognise (a newer build's side file) -> skip and count
int dm_savepkg_path_class(const char *rel, char *unit, size_t un) {
    size_t L = strlen(rel); if (!L || L > 255 || rel[0] == '/' || rel[L - 1] == '/' || !utf8_ok(rel)) return PC_UNSAFE;
    if (strchr(rel, '\\') || strchr(rel, ':')) return PC_UNSAFE;
    const char *sl = strchr(rel, '/'); char a[256], b[256] = "";
    if (sl) { if (strchr(sl + 1, '/')) return PC_UNSAFE; snprintf(a, sizeof a, "%.*s", (int)(sl - rel), rel); strlcpy(b, sl + 1, sizeof b); } else strlcpy(a, rel, sizeof a);
    if (a[0] == '.' || a[0] == '_' || b[0] == '.' || !a[0] || (sl && !b[0])) return PC_UNSAFE;
    if (!sl) { const char *d = strrchr(a, '.'); if (!d) return PC_UNKNOWN; char st[256]; snprintf(st, sizeof st, "%.*s", (int)(d - a), a);
        if (is_stash_name(st) && (!strcmp(d, ".swvault") || !strcmp(d, ".d2i"))) { strlcpy(unit, st, un); return PC_STASH; } return PC_UNKNOWN; }
    if (!strcmp(a, "Settings") && !(strncmp(b, "Settings.", 9) == 0 && char_ext_ok(b + 9))) { size_t n = strlen(b); if ((n > 5 && !strcmp(b + n - 5, ".json")) || (n > 5 && !strcmp(b + n - 5, ".fltr"))) { strlcpy(unit, "_settings", un); return PC_SETTINGS; } return PC_UNKNOWN; }
    if (is_stash_name(a)) { size_t n = strlen(a); if (!strncmp(b, a, n) && (!strcmp(b + n, ".d2i") || !strcmp(b + n, ".swvault"))) { strlcpy(unit, a, un); return PC_STASH; } return PC_UNKNOWN; }
    if (!dm_savepkg_name_ok(a)) return PC_UNKNOWN;
    size_t n = strlen(a); if (strncmp(b, a, n) || b[n] != '.' || !char_ext_ok(b + n + 1)) return PC_UNKNOWN;
    strlcpy(unit, a, un); return PC_CHAR;
}
// 「保留两份」取新名：名字后加 -A…-Z（已含 - _ 就直接加字母，符号最多一个）；超 15 字节从尾部按 UTF-8 边界截短；尾字符必须是字母
// Keep-both name: append -A..-Z (just the letter if the name already has - or _, one symbol at most); over 15 bytes the base is cut at a UTF-8 boundary; the last character must be a letter
int dm_savepkg_keepboth_name(const char *name, char *out, size_t n, int (*taken)(const char *, void *), void *ctx) {
    int sym = strchr(name, '-') || strchr(name, '_'); size_t add = sym ? 1 : 2, keep = strlen(name);
    if (keep + add > 15) { keep = 15 - add; while (keep && ((uint8_t)name[keep] & 0xc0) == 0x80) keep--; }
    while (keep && !letter((uint8_t)name[keep - 1])) { keep--; sym = 0; }   // 截短后尾部不能是符号；符号被截掉了就重新要分隔符 | no trailing symbol after the cut; if the symbol was cut off a separator is needed again
    for (char c = 'A'; c <= 'Z'; c++) { char s[32]; if (keep + (sym ? 1 : 2) > 15) return 0; snprintf(s, sizeof s, "%.*s%s%c", (int)keep, name, sym ? "" : "-", c);
        if (dm_savepkg_name_ok(s) && !(taken && taken(s, ctx))) { strlcpy(out, s, n); return 1; } }
    return 0;
}

// ---- .d2s 头（版本 0x61 / 0x69，平移与 SaveSummaryReader 相同）| .d2s header (versions 0x61 / 0x69, same layout as SaveSummaryReader)
static uint32_t d2s_ck(const uint8_t *b, size_t n) { uint32_t s = 0; for (size_t i = 0; i < n; i++) { s = (s << 1) | (s >> 31); s += i >= 12 && i < 16 ? 0 : b[i]; } return s; }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
int dm_savepkg_parse_d2s(const uint8_t *b, size_t n, dm_pkg_char *o) {
    if (n < 0x15b || n > MAX_FILE || rd32(b) != 0xaa55aa55) return 0; uint32_t ver = rd32(b + 4); if (ver != 0x61 && ver != 0x69) return 0;
    if (rd32(b + 8) != n || rd32(b + 12) != d2s_ck(b, n)) return 0;
    int cls = b[0x18], lvl = b[0x1b]; if (cls > 7 || lvl < 1 || lvl > 99) return 0;
    size_t e = 0x12b; while (e < 0x15b && b[e]) e++; if (e == 0x12b || e == 0x15b || e - 0x12b > 15) return 0;
    char nm[64]; memcpy(nm, b + 0x12b, e - 0x12b); nm[e - 0x12b] = 0; if (!utf8_ok(nm)) return 0;
    if (o) { strlcpy(o->name, nm, sizeof o->name); o->cls = cls; o->level = lvl; o->progress = b[0x15]; o->hardcore = (b[0x14] & 0x04) != 0; o->expansion = (b[0x14] & 0x20) != 0; o->last_played = rd32(b + 0x20); }
    return 1;
}
// 护身符页侧文件头（交接/护身符页_格式变更_20261031.md）：魔数 SWCH、版本 1、保留 3 字节、件数 u16，然后逐件 x、y、长度 u16、位流。大符出界和重叠要物品表，进局重建时查
// Charm page side file (see that format note): magic SWCH, version 1, 3 reserved bytes, u16 count, then per item x, y, u16 length, bitstream. Large charms out of bounds and overlaps need the item tables and are checked when the page is rebuilt on entry
int dm_savepkg_swcharm_ok(const uint8_t *b, size_t n) {
    if (n < 10 || memcmp(b, "SWCH", 4) || b[4] != 1) return 0; uint32_t cnt = rd16(b + 8); if (cnt > 84) return 0; size_t o = 10;   // 背包 12×7（rc2-storage 定稿）| the inventory is 12 x 7
    for (uint32_t i = 0; i < cnt; i++) { if (n - o < 4 || b[o] >= 12 || b[o + 1] >= 7) return 0; uint32_t L = rd16(b + o + 2); if (L < 1 || L > 1024 || n - o - 4 < L) return 0; o += 4 + (size_t)L; }
    return o == n;
}
int dm_savepkg_rename_d2s(uint8_t *b, size_t n, const char *newname) {
    size_t L = strlen(newname); if (n < 0x15b || L > 15 || !dm_savepkg_parse_d2s(b, n, NULL)) return 0;
    memset(b + 0x12b, 0, 0x30); memcpy(b + 0x12b, newname, L); uint32_t c = d2s_ck(b, n); b[12] = (uint8_t)c; b[13] = (uint8_t)(c >> 8); b[14] = (uint8_t)(c >> 16); b[15] = (uint8_t)(c >> 24); return 1;
}

// ---- 联机角色隔离（用户 2026-10-31 02:2x，防作弊：联机存档禁止任何形式互通 / 导入 / 导出）。按联机线 W8 v0.2（COOP_PROTOCOL §8.3 P-003）：联机角色在单机存档根的兄弟目录 <存档根>.coop/ 里，标记 <根>/<角色目录>/<角色>.sw_coop。枚举只走单机根，所以兄弟目录本来就不进包；存档根里出现带标记的角色也一样排除，算第二道。
// 导出和备份都不带联机角色；联机根本身不能当导出来源；包里有标记就整包拒绝；同名的本机联机角色不被覆盖。没有签名，改过的包可以去掉标记，所以这只是这一端的隔离，真正的防线是联机主机校验
// ---- Co-op character isolation (user, 2026-10-31, anti-cheat: co-op saves must not move in any form). Per co-op line W8 v0.2 (COOP_PROTOCOL §8.3 P-003): co-op characters live in the sibling <save root>.coop/, marked by <root>/<character dir>/<character>.sw_coop. Enumeration only walks the single-player root, so the sibling never enters a package; a marked character inside the save root is excluded as well, as a second line.
// 联机根看字面路径和解析符号链接后的真实路径（任一段），导出（含快照的来源根）、备份、导入目标、换入用同一个判定 | The co-op root is judged on the literal path and on the resolved real path (any segment); export (including a snapshot's source root), backup, import target and apply share this check
// Exports and backups leave co-op characters out; the co-op root itself cannot be an export source; a package with a marker is refused whole; a local co-op character of the same name is never overwritten. Nothing is signed and a tampered package can drop the marker, so this is only this side's isolation; the real defence is the co-op host's validation
static int seg_ends(const char *s, size_t n, const char *t) { size_t m = strlen(t); return n >= m && !strncasecmp(s + n - m, t, m); }
// 任一路径段以 .coop 或 .sw_coop 结尾就算（角色名不能含点，不会误伤；大小写和反斜杠按其他文件系统上写出来的算）| any path segment ending in .coop or .sw_coop counts (character names cannot contain a dot, so nothing legal is hit; case and backslashes as another filesystem may have written them)
static int coop_path(const char *rel) { for (const char *s = rel; ; ) { size_t a = strcspn(s, "/\\"); if (seg_ends(s, a, ".coop") || seg_ends(s, a, ".sw_coop")) return 1; if (!s[a]) return 0; s += a + 1; } }
static int coop_root(const char *p) { if (coop_path(p)) return 1; pth q; strlcpy(q, p, sizeof q); for (;;) { char *r = realpath(q, NULL); if (r) { int hit = coop_path(r); free(r); return hit; } char *sl = strrchr(q, '/'); if (!sl || sl == q) return 0; *sl = 0; } }   // 目录还没建时看最近的上级 | a directory not created yet is judged by its nearest parent
int dm_savepkg_is_coop_root(const char *path) { return coop_root(path); }
int dm_savepkg_is_coop_char(const char *root, const char *phys) { pth d; snprintf(d, sizeof d, "%s/%s", root, phys); DIR *dp = opendir(d); if (!dp) return 0; struct dirent *e; int hit = 0; while (!hit && (e = readdir(dp))) hit = coop_path(e->d_name); closedir(dp); return hit; }
static int coop_count_in(const char *root) { DIR *dp = opendir(root); if (!dp) return 0; struct dirent *e; int n = 0; while ((e = readdir(dp))) { if (e->d_name[0] == '.') continue; n += dm_savepkg_is_coop_char(root, e->d_name); } closedir(dp); return n; }
int dm_savepkg_coop_count(const char *save) { pth sib; snprintf(sib, sizeof sib, "%s.coop", save); return coop_count_in(save) + coop_count_in(sib); }

// ---- 名单文件（根目录 sw_elysium.txt / dm_stash_topup.txt，一行一个角色名，按行跟角色走）| Name lists (one character per line; lines travel with the character)
static int list_has(const char *root, const char *file, const char *name) { pth p; snprintf(p, sizeof p, "%s/%s", root, file); FILE *f = fopen(p, "r"); if (!f) return 0; char ln[128]; int hit = 0; while (!hit && fgets(ln, sizeof ln, f)) { ln[strcspn(ln, "\r\n")] = 0; hit = !strcmp(ln, name); } fclose(f); return hit; }
static int list_set(const char *root, const char *file, const char *name, int on) {
    pth p; snprintf(p, sizeof p, "%s/%s", root, file); size_t n = 0; uint8_t *b = read_all(p, 1 << 20, &n); char *out = malloc(n + 128); if (!out) { free(b); return -1; } size_t o = 0; int had = 0;
    if (b) for (char *s = tok((char *)b, "\r\n"); s; s = tok(NULL, "\r\n")) { if (!strcmp(s, name)) { had = 1; if (!on) continue; } o += (size_t)sprintf(out + o, "%s\n", s); }
    if (on && !had) o += (size_t)sprintf(out + o, "%s\n", name);
    free(b); int rc = 0; if (on != had) { if (!o) { unlink(p); } else rc = write_replace(p, out, o); } free(out); return rc;
}

// ---- 枚举存档树（按客体名：APFS 上的物理别名还原，见 hle_fs.c）| Walk a save tree by guest name (physical aliases on APFS are resolved, see hle_fs.c)
typedef struct { char rel[256], unit[64], phys[1400]; int cls; long long size; int64_t mt; char sha[65]; } pf;
typedef struct { pf *v; int n, cap, skipped, coop; } pfl;
static int guest_of(const char *parent, const char *phys, char *o, size_t n) { pth p; snprintf(p, sizeof p, "%s/%s", parent, phys); ssize_t k = PKG_GUEST_GET(p, o, n - 1); if (k >= 0) { o[k] = 0; return 1; } strlcpy(o, phys, n); return 1; }
static int pf_add(pfl *l, const char *rel, const char *phys, const struct stat *st) {
    char unit[64] = ""; int c = dm_savepkg_path_class(rel, unit, sizeof unit); if (c <= 0) { l->skipped++; return 0; }
    if (l->n >= MAX_FILES) return -1; if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 128; pf *nv = realloc(l->v, sizeof *nv * (size_t)l->cap); if (!nv) return -1; l->v = nv; }
    pf *x = &l->v[l->n++]; memset(x, 0, sizeof *x); strlcpy(x->rel, rel, sizeof x->rel); strlcpy(x->unit, unit, sizeof x->unit); strlcpy(x->phys, phys, sizeof x->phys); x->cls = c; x->size = st->st_size;
#ifdef __APPLE__
    x->mt = (int64_t)st->st_mtimespec.tv_sec * 1000 + st->st_mtimespec.tv_nsec / 1000000;
#else
    x->mt = (int64_t)st->st_mtim.tv_sec * 1000 + st->st_mtim.tv_nsec / 1000000;
#endif
    return 0;
}
static int cmp_pf(const void *a, const void *b) { return strcmp(((const pf *)a)->rel, ((const pf *)b)->rel); }
static int walk(const char *root, pfl *l) {
    DIR *d = opendir(root); if (!d) return -1; struct dirent *e; int rc = 0;
    while (!rc && (e = readdir(d))) { if (e->d_name[0] == '.') continue; pth full; snprintf(full, sizeof full, "%s/%s", root, e->d_name); struct stat st; if (lstat(full, &st)) continue;
        char g[256]; guest_of(root, e->d_name, g, sizeof g);
        if (S_ISREG(st.st_mode)) { rc = pf_add(l, g, full, &st); continue; } if (!S_ISDIR(st.st_mode)) continue; if (g[0] == '_') continue; if (coop_path(g) || dm_savepkg_is_coop_char(root, e->d_name)) { l->coop++; continue; }   // 联机角色整个不进来 | co-op characters stay out entirely
        DIR *d2 = opendir(full); if (!d2) continue; struct dirent *e2;
        while (!rc && (e2 = readdir(d2))) { if (e2->d_name[0] == '.') continue; pth f2; snprintf(f2, sizeof f2, "%s/%s", full, e2->d_name); struct stat s2; if (lstat(f2, &s2) || !S_ISREG(s2.st_mode)) continue;
            char g2[256], rel[600]; guest_of(full, e2->d_name, g2, sizeof g2); snprintf(rel, sizeof rel, "%s/%s", g, g2); rc = pf_add(l, rel, f2, &s2); }
        closedir(d2); }
    closedir(d); if (!rc) qsort(l->v, (size_t)l->n, sizeof *l->v, cmp_pf); return rc;
}
// 进度（文件数）：导出、校验在工作线程写，面板读。QA 构建可放慢，真存档太小，看不到进度条（§2.9）| progress (files): written by the worker in export and inspect, read by the panel. QA builds can slow it down: real saves are too small to see the bar
static _Atomic int g_prog_done, g_prog_total;
void dm_savepkg_progress_read(int *done, int *total) { *done = atomic_load(&g_prog_done); *total = atomic_load(&g_prog_total); }
static void prog(int done, int total) {
    atomic_store(&g_prog_total, total); atomic_store(&g_prog_done, done);
#ifdef DM_QA_BUILD
    { const char *slow = getenv("DM_TEST_SAVE_SLOW"); if (slow) usleep((useconds_t)atoi(slow) * 1000); }
#endif
}
static int hash_file(pf *x) { size_t n = 0; uint8_t *b = read_all(x->phys, MAX_FILE, &n); if (!b) return -1; x->size = (long long)n; sha_hex(b, n, x->sha); free(b); return 0; }
// 单位摘要：跨端稳定（路径按字节序 + 每个文件的 SHA-256）| unit digest, stable across platforms (paths in byte order + each file's SHA-256)
static void unit_sha(const pfl *l, const char *unit, char o[65]) { CC_SHA256_CTX c; CC_SHA256_Init(&c); for (int i = 0; i < l->n; i++) if (!strcmp(l->v[i].unit, unit)) { CC_SHA256_Update(&c, l->v[i].rel, (CC_LONG)strlen(l->v[i].rel) + 1); CC_SHA256_Update(&c, l->v[i].sha, 64); } uint8_t h[CC_SHA256_DIGEST_LENGTH]; CC_SHA256_Final(h, &c); hex(h, sizeof h, o); }

// ---- JSON（严格最小解析器：整段合法才算，重复键算错，深度封顶）| JSON (strict minimal parser: the whole text must be valid, duplicate keys are errors, depth is capped)
enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ };
typedef struct jn { int t; long long i; char *s, *key; struct jn *kid, *next; } jn;
static void jfree(jn *n) { while (n) { jn *x = n->next; jfree(n->kid); free(n->s); free(n->key); free(n); n = x; } }
static void ws(const char **p) { while (**p == ' ' || **p == '\n' || **p == '\r' || **p == '\t') (*p)++; }
static int hex4(const char *p) { int v = 0; for (int i = 0; i < 4; i++) { char c = p[i]; v <<= 4; if (c >= '0' && c <= '9') v |= c - '0'; else if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f') v |= (c | 0x20) - 'a' + 10; else return -1; } return v; }
static char *jstr(const char **pp) {
    const char *p = *pp; if (*p != '"') return NULL; p++; size_t cap = 32, o = 0; char *b = malloc(cap); if (!b) return NULL;
    while (*p != '"') { uint8_t c = (uint8_t)*p; if (!c || c < 0x20) { free(b); return NULL; } if (o + 8 > cap) { cap *= 2; char *nb = realloc(b, cap); if (!nb) { free(b); return NULL; } b = nb; }
        if (c != '\\') { b[o++] = (char)c; p++; continue; } p++; c = (uint8_t)*p++;
        switch (c) { case '"': case '\\': case '/': b[o++] = (char)c; break; case 'b': b[o++] = 8; break; case 'f': b[o++] = 12; break; case 'n': b[o++] = 10; break; case 'r': b[o++] = 13; break; case 't': b[o++] = 9; break;
            case 'u': { int u = hex4(p); if (u < 0) { free(b); return NULL; } p += 4; if (u >= 0xd800 && u < 0xdc00) { if (p[0] != '\\' || p[1] != 'u') { free(b); return NULL; } int l = hex4(p + 2); if (l < 0xdc00 || l > 0xdfff) { free(b); return NULL; } p += 6; u = 0x10000 + ((u - 0xd800) << 10) + (l - 0xdc00); } else if (u >= 0xdc00 && u < 0xe000) { free(b); return NULL; }
                if (!u) { free(b); return NULL; } if (u < 0x80) b[o++] = (char)u; else if (u < 0x800) { b[o++] = (char)(0xc0 | u >> 6); b[o++] = (char)(0x80 | (u & 63)); } else if (u < 0x10000) { b[o++] = (char)(0xe0 | u >> 12); b[o++] = (char)(0x80 | ((u >> 6) & 63)); b[o++] = (char)(0x80 | (u & 63)); } else { b[o++] = (char)(0xf0 | u >> 18); b[o++] = (char)(0x80 | ((u >> 12) & 63)); b[o++] = (char)(0x80 | ((u >> 6) & 63)); b[o++] = (char)(0x80 | (u & 63)); } break; }
            default: free(b); return NULL; } }
    b[o] = 0; *pp = p + 1; return b;
}
static jn *jval(const char **pp, int depth) {
    if (depth > 8) return NULL; const char *p = *pp; ws(&p); jn *n = calloc(1, sizeof *n); if (!n) return NULL;
    if (*p == '{' || *p == '[') { int obj = *p == '{'; n->t = obj ? J_OBJ : J_ARR; p++; ws(&p); jn **tail = &n->kid;
        if (*p == (obj ? '}' : ']')) p++; else for (;;) { char *key = NULL; if (obj) { ws(&p); key = jstr(&p); if (!key) goto bad; for (jn *k = n->kid; k; k = k->next) if (!strcmp(k->key, key)) { free(key); goto bad; } ws(&p); if (*p != ':') { free(key); goto bad; } p++; }
            jn *c = jval(&p, depth + 1); if (!c) { free(key); goto bad; } c->key = key; *tail = c; tail = &c->next; ws(&p); if (*p == ',') { p++; continue; } if (*p == (obj ? '}' : ']')) { p++; break; } goto bad; } }
    else if (*p == '"') { n->t = J_STR; n->s = jstr(&p); if (!n->s) goto bad; }
    else if (!strncmp(p, "true", 4)) { n->t = J_BOOL; n->i = 1; p += 4; } else if (!strncmp(p, "false", 5)) { n->t = J_BOOL; p += 5; } else if (!strncmp(p, "null", 4)) { p += 4; }
    else if (*p == '-' || (*p >= '0' && *p <= '9')) { char *e; n->t = J_NUM; n->i = strtoll(p, &e, 10); if (e == p || (*p == '-' && e == p + 1)) goto bad; if (*e == '.' || *e == 'e' || *e == 'E') { strtod(p, &e); } p = e; }
    else goto bad;
    *pp = p; return n;
bad: jfree(n); return NULL;
}
static jn *jget(const jn *o, const char *k) { if (!o || o->t != J_OBJ) return NULL; for (jn *c = o->kid; c; c = c->next) if (!strcmp(c->key, k)) return c; return NULL; }
static const char *jstrv(const jn *o, const char *k) { jn *c = jget(o, k); return c && c->t == J_STR ? c->s : ""; }
static long long jnum(const jn *o, const char *k, long long def) { jn *c = jget(o, k); return c && (c->t == J_NUM || c->t == J_BOOL) ? c->i : def; }
static int jflag(const jn *o, const char *k) { jn *c = jget(o, k); return c && c->t != J_NULL && !((c->t == J_NUM || c->t == J_BOOL) && !c->i) && !(c->t == J_STR && !c->s[0]); }   // 键在、值不是 false / 0 / null / 空串 | key present and not false / 0 / null / empty
static void jput(FILE *f, const char *s) { fputc('"', f); for (; *s; s++) { uint8_t c = (uint8_t)*s; if (c == '"' || c == '\\') { fputc('\\', f); fputc(c, f); } else if (c < 0x20) fprintf(f, "\\u%04x", c); else fputc(c, f); } fputc('"', f); }

// ---- 可移植设置白名单（设计稿 1.5：图形 / 实验项 / iCloud / 语言一律不带）| Portable settings allow-list (graphics / experimental / iCloud / language never travel)
static const char *const k_custom_ok[] = { "auto_mana", "merc_pot", "drop_light", "map_mark", "item_notes", "pot_pick", "drop_haptic", "gem_pick", "store_btn", "loot_filter", "auto_buff", "touch_swap_above", "free_tp" };
static int custom_ok(const char *k) { for (size_t i = 0; i < sizeof k_custom_ok / sizeof *k_custom_ok; i++) if (!strcmp(k, k_custom_ok[i])) return 1; return 0; }
static int custom_valid(const char *k, const char *v) { return custom_ok(k) && (!strcmp(v, "0") || !strcmp(v, "1")); }
static int custom_merge(const char *dir, const char *const *kv, int n) {   // kv = 键, 值, 键, 值…；只认白名单；别的键一字不动 | kv = key, value, ...; allow-listed keys only; other keys stay as they are
    pth p; snprintf(p, sizeof p, "%s/sw_custom.txt", dir); size_t len = 0; uint8_t *b = read_all(p, 1 << 20, &len); size_t cap = len + 4096; char *out = malloc(cap); if (!out) { free(b); return -1; } size_t o = 0;
    if (b) for (char *s = tok((char *)b, "\r\n"); s; s = tok(NULL, "\r\n")) { char *eq = strchr(s, '='); int drop = 0; if (eq) { *eq = 0; for (int i = 0; i < n; i++) if (custom_valid(kv[2 * i], kv[2 * i + 1])) { if (!strcmp(s, kv[2 * i])) drop = 1; if (!strcmp(s, "merc_pot_v2") && !strcmp(kv[2 * i], "merc_pot")) drop = 1; } *eq = '='; } if (!drop) o += (size_t)snprintf(out + o, cap - o, "%s\n", s); }
    for (int i = 0; i < n; i++) { if (!custom_valid(kv[2 * i], kv[2 * i + 1])) continue; o += (size_t)snprintf(out + o, cap - o, "%s=%s\n", kv[2 * i], kv[2 * i + 1]); if (!strcmp(kv[2 * i], "merc_pot")) o += (size_t)snprintf(out + o, cap - o, "merc_pot_v2=1\n"); }
    free(b); int rc = write_replace(p, out, o); free(out); return rc;
}

// ---- ZIP 写（只写 stored；时间固定 1980，真实时间只在 manifest）| ZIP writer (stored only; time fixed at 1980, real times live in the manifest only)
static uint32_t crc32_of(const uint8_t *d, size_t n) { static uint32_t t[256]; if (!t[1]) for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1; t[i] = c; } uint32_t c = ~0u; for (size_t i = 0; i < n; i++) c = t[(c ^ d[i]) & 255] ^ (c >> 8); return ~c; }
typedef struct { char name[256]; uint32_t crc, size, off; } ze;
static void w16(FILE *f, uint32_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
static void w32(FILE *f, uint32_t v) { w16(f, v & 0xffff); w16(f, v >> 16); }
static int zip_put(FILE *f, ze *e, const char *name, const uint8_t *d, size_t n) {
    strlcpy(e->name, name, sizeof e->name); e->crc = crc32_of(d, n); e->size = (uint32_t)n; e->off = (uint32_t)ftell(f); size_t L = strlen(name);
    w32(f, 0x04034b50); w16(f, 20); w16(f, 0x0800); w16(f, 0); w16(f, 0); w16(f, 0x21); w32(f, e->crc); w32(f, e->size); w32(f, e->size); w16(f, (uint32_t)L); w16(f, 0);
    return fwrite(name, 1, L, f) == L && (!n || fwrite(d, 1, n, f) == n) ? 0 : -1;
}
static int zip_end(FILE *f, const ze *z, int n) {
    uint32_t cd = (uint32_t)ftell(f);
    for (int i = 0; i < n; i++) { size_t L = strlen(z[i].name); w32(f, 0x02014b50); w16(f, 20); w16(f, 20); w16(f, 0x0800); w16(f, 0); w16(f, 0); w16(f, 0x21); w32(f, z[i].crc); w32(f, z[i].size); w32(f, z[i].size); w16(f, (uint32_t)L); w16(f, 0); w16(f, 0); w16(f, 0); w16(f, 0); w32(f, 0); w32(f, z[i].off); if (fwrite(z[i].name, 1, L, f) != L) return -1; }
    uint32_t sz = (uint32_t)ftell(f) - cd; w32(f, 0x06054b50); w16(f, 0); w16(f, 0); w16(f, (uint32_t)n); w16(f, (uint32_t)n); w32(f, sz); w32(f, cd); w16(f, 0); return ferror(f) ? -1 : 0;
}

// ---- 导出 | Export
static int sel_has(const dm_pkg_export_opts *o, const char *name) { if (o->nnames < 0) return 1; for (int i = 0; i < o->nnames; i++) if (!strcmp(o->names[i], name)) return 1; return 0; }
static void iso_utc(int64_t t, char o[24]) { time_t tt = (time_t)t; struct tm tm; gmtime_r(&tt, &tm); strftime(o, 24, "%Y-%m-%dT%H:%M:%SZ", &tm); }
int dm_savepkg_export(const char *tree, const char *out_zip, const dm_pkg_export_opts *o, const dm_pkg_source *src) {
    if (coop_root(tree) || (o->source_root && coop_root(o->source_root))) { LOG("存档管理：联机存档根不能导出"); return DM_PKG_E_COOP; }
    pfl all = {0}; if (walk(tree, &all)) { free(all.v); return DM_PKG_E_IO; }
    int rc = DM_PKG_E_IO; int n = 0; for (int i = 0; i < all.n; i++) { pf *x = &all.v[i]; int keep = x->cls == PC_CHAR ? sel_has(o, x->unit) : x->cls == PC_STASH ? o->stash : o->settings; if (keep) all.v[n++] = *x; } all.n = n;
    if (all.n >= MAX_FILES) { free(all.v); return DM_PKG_E_LIMIT; }
    long long total = 0; for (int i = 0; i < all.n; i++) { if (hash_file(&all.v[i])) { LOG("存档管理：导出读文件失败 errno=%d", errno); free(all.v); return DM_PKG_E_IO; } total += all.v[i].size; if (all.v[i].size > MAX_FILE || total > MAX_TOTAL) { free(all.v); return DM_PKG_E_LIMIT; } }
    char *mj = NULL; size_t mjn = 0; FILE *m = open_memstream(&mj, &mjn); if (!m) { free(all.v); return DM_PKG_E_IO; } char when[24]; iso_utc(o->now, when);
    fprintf(m, "{\n\"format\":\"d2rmp-saves\",\"format_major\":%d,\"format_minor\":%d,\"exported_at\":\"%s\",\"kind\":", DM_PKG_FORMAT_MAJOR, DM_PKG_FORMAT_MINOR, when); jput(m, o->kind ? o->kind : "export");
    fprintf(m, ",\n\"source\":{\"platform\":"); jput(m, src && src->platform ? src->platform : ""); fprintf(m, ",\"commit\":"); jput(m, src && src->commit ? src->commit : ""); fprintf(m, ",\"build\":"); jput(m, src && src->build ? src->build : ""); fprintf(m, ",\"version\":"); jput(m, src && src->version ? src->version : "");
    fprintf(m, "},\n\"game_build\":"); jput(m, src && src->game_build ? src->game_build : ""); fprintf(m, ",\"source_format\":"); jput(m, o->source_format ? o->source_format : "d2r-switch-plus"); fprintf(m, ",\"source_version\":"); jput(m, src && src->game_build ? src->game_build : ""); fprintf(m, ",\"converted_from\":null,\"save_gen\":%d,\n\"includes\":{\"shared_stash\":%s,\"settings\":%s},\n\"characters\":[", DM_PKG_SAVE_GEN, o->stash ? "true" : "false", o->settings ? "true" : "false");
    int first = 1; for (int i = 0; i < all.n; i++) { pf *x = &all.v[i]; size_t ul = strlen(x->unit); if (x->cls != PC_CHAR || strncmp(x->rel + ul + 1, x->unit, ul) || strcmp(x->rel + ul + 1 + ul, ".d2s")) continue;
        size_t bn = 0; uint8_t *b = read_all(x->phys, MAX_FILE, &bn); dm_pkg_char c; memset(&c, 0, sizeof c); int ok = b && dm_savepkg_parse_d2s(b, bn, &c); free(b); if (!ok) { LOG("存档管理：导出时有 1 个角色档校验不过，跳过这一条的摘要"); }
        char us[65]; unit_sha(&all, x->unit, us); fprintf(m, "%s\n{\"name\":", first ? "" : ","); jput(m, x->unit); fprintf(m, ",\"class\":%d,\"level\":%d,\"progress\":%d,\"last_played\":%u,\"hardcore\":%s,\"expansion\":%s,\"elysium\":%s,\"stash_topup\":%s,\"valid\":%s,\"unit_sha256\":\"%s\"}", c.cls, c.level, c.progress, c.last_played, c.hardcore ? "true" : "false", c.expansion ? "true" : "false", list_has(tree, "sw_elysium.txt", x->unit) ? "true" : "false", list_has(tree, "dm_stash_topup.txt", x->unit) ? "true" : "false", ok ? "true" : "false", us); first = 0; }
    fprintf(m, "],\n\"stash\":["); first = 1; for (int i = 0; i < all.n; i++) { pf *x = &all.v[i]; if (x->cls != PC_STASH) continue; int seen = 0; for (int k = 0; k < i; k++) if (all.v[k].cls == PC_STASH && !strcmp(all.v[k].unit, x->unit)) seen = 1; if (seen) continue; char us[65]; unit_sha(&all, x->unit, us); fprintf(m, "%s{\"name\":", first ? "" : ","); jput(m, x->unit); fprintf(m, ",\"unit_sha256\":\"%s\"}", us); first = 0; }
    fprintf(m, "],\n\"custom\":{"); first = 1;
    if (o->settings && o->custom_dir) { pth cp; snprintf(cp, sizeof cp, "%s/sw_custom.txt", o->custom_dir); size_t cn = 0; uint8_t *cb = read_all(cp, 1 << 20, &cn); if (cb) { for (char *s = tok((char *)cb, "\r\n"); s; s = tok(NULL, "\r\n")) { char *eq = strchr(s, '='); if (!eq) continue; *eq = 0; if (custom_ok(s) && (!strcmp(eq + 1, "0") || !strcmp(eq + 1, "1"))) { fprintf(m, "%s", first ? "" : ","); jput(m, s); fprintf(m, ":%s", eq + 1); first = 0; } } free(cb); } }
    fprintf(m, "},\n\"files\":["); for (int i = 0; i < all.n; i++) { pf *x = &all.v[i]; fprintf(m, "%s\n{\"path\":", i ? "," : ""); jput(m, x->rel); fprintf(m, ",\"unit\":"); jput(m, x->unit); fprintf(m, ",\"size\":%lld,\"mtime_ms\":%lld,\"sha256\":\"%s\"}", x->size, (long long)x->mt, x->sha); } fprintf(m, "]\n}\n"); fclose(m);
    pth tmp; snprintf(tmp, sizeof tmp, "%s.tmp", out_zip); unlink(tmp); FILE *f = fopen(tmp, "wb"); ze *z = calloc((size_t)all.n + 1, sizeof *z);
    if (f && z && !test_fail("export_write") && !zip_put(f, &z[0], "manifest.json", (const uint8_t *)mj, mjn)) { rc = DM_PKG_OK;
        for (int i = 0; i < all.n && !rc; i++) { size_t bn = 0; uint8_t *b = read_all(all.v[i].phys, MAX_FILE, &bn); if (!b || zip_put(f, &z[i + 1], all.v[i].rel, b, bn)) rc = DM_PKG_E_IO; free(b); prog(i + 1, all.n); if (!rc && o->progress && o->progress(i + 1, all.n)) rc = DM_PKG_E_IO; }
        if (!rc && zip_end(f, z, all.n + 1)) rc = DM_PKG_E_IO; }
    if (f && fflush(f)) rc = DM_PKG_E_IO; if (f && !rc && fsync(fileno(f))) rc = DM_PKG_E_IO; if (f) fclose(f); if (!rc && rename(tmp, out_zip)) rc = DM_PKG_E_IO; if (rc) unlink(tmp);
    LOG("存档管理：导出 %s（%d 个文件，跳过 %d 个不属于存档的文件、%d 个联机角色）", rc ? "失败" : "完成", all.n, all.skipped, all.coop);
    free(mj); free(z); free(all.v); return rc;
}

// ---- ZIP 读（只信结构校验过的中央目录；任何不合规都整包拒绝）| ZIP reader (only a structurally validated central directory is trusted; anything off rejects the package)
typedef struct { char name[512]; uint32_t csize, usize, crc, off; uint64_t data; int method; } zr;
typedef struct { FILE *f; long long size; zr *e; int n; uint64_t cd; } zf;
static void zf_close(zf *z) { if (z->f) fclose(z->f); free(z->e); memset(z, 0, sizeof *z); }
static int cmp_zr_off(const void *a, const void *b) { uint64_t x = ((const zr *)a)->off, y = ((const zr *)b)->off; return x < y ? -1 : x > y; }
static int cmp_zr_name(const void *a, const void *b) { return strcmp(((const zr *)a)->name, ((const zr *)b)->name); }
static int zf_open(zf *z, const char *path) {
    memset(z, 0, sizeof *z); z->f = fopen(path, "rb"); if (!z->f) return DM_PKG_E_IO; struct stat st; if (fstat(fileno(z->f), &st) || !S_ISREG(st.st_mode)) return DM_PKG_E_IO;
    z->size = st.st_size; if (z->size < 22) return DM_PKG_E_ZIP; if (z->size > MAX_TOTAL + (64ll << 20)) return DM_PKG_E_LIMIT;
    size_t tail = z->size < 65557 ? (size_t)z->size : 65557; uint8_t *t = malloc(tail); if (!t) return DM_PKG_E_IO; if (fseeko(z->f, z->size - (off_t)tail, SEEK_SET) || fread(t, 1, tail, z->f) != tail) { free(t); return DM_PKG_E_IO; }
    long at = -1; for (long i = (long)tail - 22; i >= 0; i--) if (rd32(t + i) == 0x06054b50 && (size_t)i + 22 + rd16(t + i + 20) == tail) { at = i; break; } if (at < 0) { free(t); return DM_PKG_E_ZIP; }
    const uint8_t *q = t + at; uint32_t disk = rd16(q + 4), cdd = rd16(q + 6), cnt = rd16(q + 8), tot = rd16(q + 10), cds = rd32(q + 12), cdo = rd32(q + 16); uint64_t eocd = (uint64_t)z->size - tail + (uint64_t)at; free(t);
    if (disk || cdd || cnt != tot) return DM_PKG_E_ZIP; if (tot == 0xffff || cds == 0xffffffffu || cdo == 0xffffffffu) return DM_PKG_E_ZIP; if (tot > MAX_ZIP_ENTRIES) return DM_PKG_E_LIMIT; if ((uint64_t)cdo + cds > eocd || !tot) return DM_PKG_E_ZIP;
    uint8_t *cd = malloc(cds + 1); if (!cd) return DM_PKG_E_IO; if (fseeko(z->f, cdo, SEEK_SET) || fread(cd, 1, cds, z->f) != cds) { free(cd); return DM_PKG_E_IO; }
    z->e = calloc(tot, sizeof *z->e); if (!z->e) { free(cd); return DM_PKG_E_IO; } z->cd = cdo; uint32_t p = 0; int rc = DM_PKG_OK;
    for (uint32_t i = 0; i < tot && !rc; i++) { if ((uint64_t)p + 46 > cds || rd32(cd + p) != 0x02014b50) { rc = DM_PKG_E_ZIP; break; } const uint8_t *c = cd + p; uint32_t made = rd16(c + 4), fl = rd16(c + 8), me = rd16(c + 10), crc = rd32(c + 16), cs = rd32(c + 20), us = rd32(c + 24), nl = rd16(c + 28), xl = rd16(c + 30), ml = rd16(c + 32), dsk = rd16(c + 34), ext = rd32(c + 38), off = rd32(c + 42);
        if ((uint64_t)p + 46 + nl + xl + ml > cds || nl == 0 || nl >= sizeof z->e[0].name) { rc = DM_PKG_E_ZIP; break; } if (fl & 1) { rc = DM_PKG_E_ZIP; break; } if (me != 0 && me != 8) { rc = DM_PKG_E_ZIP; break; } if (dsk || cs == 0xffffffffu || us == 0xffffffffu || off == 0xffffffffu) { rc = DM_PKG_E_ZIP; break; }
        if ((made >> 8) == 3 && ((ext >> 16) & 0170000) == 0120000) { rc = DM_PKG_E_PATH; break; } if (memchr(c + 46, 0, nl)) { rc = DM_PKG_E_PATH; break; } if (me == 0 && cs != us) { rc = DM_PKG_E_ZIP; break; }
        zr *x = &z->e[i]; memcpy(x->name, c + 46, nl); x->name[nl] = 0; x->csize = cs; x->usize = us; x->crc = crc; x->off = off; x->method = (int)me; p += 46 + nl + xl + ml; }
    free(cd); if (rc) return rc; z->n = (int)tot;
    for (int i = 0; i < z->n; i++) { zr *x = &z->e[i]; uint8_t h[30]; size_t L = strlen(x->name); char nm[512]; if ((uint64_t)x->off + 30 + L > cdo || fseeko(z->f, x->off, SEEK_SET) || fread(h, 1, 30, z->f) != 30 || rd32(h) != 0x04034b50 || rd16(h + 26) != L || fread(nm, 1, L, z->f) != L || memcmp(nm, x->name, L)) return DM_PKG_E_ZIP; if (rd16(h + 6) & 1) return DM_PKG_E_ZIP; x->data = (uint64_t)x->off + 30 + L + rd16(h + 28); if (x->data + x->csize > cdo) return DM_PKG_E_ZIP; }
    qsort(z->e, (size_t)z->n, sizeof *z->e, cmp_zr_off); for (int i = 1; i < z->n; i++) if (z->e[i - 1].data + z->e[i - 1].csize > z->e[i].off) return DM_PKG_E_ZIP;   // 本地头 / 数据重叠 | overlapping local headers or data
    qsort(z->e, (size_t)z->n, sizeof *z->e, cmp_zr_name); for (int i = 1; i < z->n; i++) if (!strcmp(z->e[i - 1].name, z->e[i].name)) return DM_PKG_E_ZIP;   // 重复路径 | duplicate paths
    return DM_PKG_OK;
}
static const zr *zf_find(const zf *z, const char *name) { zr k; strlcpy(k.name, name, sizeof k.name); return bsearch(&k, z->e, (size_t)z->n, sizeof *z->e, cmp_zr_name); }
static uint8_t *zf_read(zf *z, const zr *e, uint32_t cap, int *err) {   // 出来的字节数不会超过声明大小（防压缩炸弹），CRC 对才算 | output never exceeds the declared size (zip bombs), and the CRC must match
    if (e->usize > cap) { *err = DM_PKG_E_LIMIT; return NULL; } if (e->method == 8 && e->csize > cap + (1u << 16)) { *err = DM_PKG_E_LIMIT; return NULL; }
    uint8_t *in = malloc((size_t)e->csize + 1); if (!in) { *err = DM_PKG_E_IO; return NULL; } if (fseeko(z->f, (off_t)e->data, SEEK_SET) || fread(in, 1, e->csize, z->f) != e->csize) { free(in); *err = DM_PKG_E_ZIP; return NULL; }
    uint8_t *out = in; if (e->method == 8) { out = malloc((size_t)e->usize + 1); struct libdeflate_decompressor *d = libdeflate_alloc_decompressor(); size_t act = 0; enum libdeflate_result r = out && d ? libdeflate_deflate_decompress(d, in, e->csize, out, e->usize, &act) : LIBDEFLATE_BAD_DATA; if (d) libdeflate_free_decompressor(d); free(in); if (r != LIBDEFLATE_SUCCESS || act != e->usize) { free(out); *err = DM_PKG_E_CHECK; return NULL; } }
    if (crc32_of(out, e->usize) != e->crc) { free(out); *err = DM_PKG_E_CHECK; return NULL; } out[e->usize] = 0; return out;
}
// 不在 manifest 里、可以不当回事的条目（压缩工具 / 系统留下的）；其他的整包拒绝 | entries outside the manifest that are harmless leftovers of tools and systems; anything else rejects the package
static int zip_traverses(const char *n) { if (n[0] == '/') return 1; for (const char *s = n; ; ) { size_t a = strcspn(s, "/\\"); if (a == 2 && !strncmp(s, "..", 2)) return 1; if (!s[a]) return 0; s += a + 1; } }
static int zip_junk(const char *n) {
    size_t L = strlen(n); if (L && n[L - 1] == '/') return 1; if (!strncmp(n, "__MACOSX/", 9)) return 1; const char *b = strrchr(n, '/'); b = b ? b + 1 : n;
    if (!strcmp(b, ".DS_Store") || !strcmp(b, "Thumbs.db") || !strcmp(b, "desktop.ini")) return 1; size_t bl = strlen(b);
#if !defined(__ANDROID__)   // 云端占位文件只在苹果端出现，导出时就已跳过；词不进安卓包（§2.8） | cloud placeholders exist only on Apple platforms and are already skipped on export; the word stays out of the Android package
    if (b[0] == '.' && bl > 7 && !strcmp(b + bl - 7, ".icloud")) return 1;
#endif
    const char *dot = strrchr(b, '.'); size_t st = dot ? (size_t)(dot - b) : bl; if (st > 2 && b[st - 2] == ' ' && b[st - 1] >= '2' && b[st - 1] <= '9') return 1;   // iCloud 冲突副本「名字 2.xxx」 | iCloud conflict copies "name 2.xxx"
    return 0;
}

// 导入流程的「转换」一步（第二期接外来格式，设计稿第 9 节）。source_format 缺省 = 我们自己的；有 converted_from = 文件已是我们的格式。
// 第一期只有恒等转换：我们的格式直接过；外来格式没转换过的拒绝（不把它们的文件当我们的写进存档）。files = 解好的文件目录，第二期在这里改写
// The "convert" step of import (phase 2 plugs foreign formats in here, design doc section 9). A missing source_format = our own; converted_from present = the files are already in our format.
// Phase 1 only has the identity conversion: our format passes; an unconverted foreign format is refused (its files must not be written into the save as if they were ours). files = the extracted directory, which phase 2 rewrites here
// 上游写存档的代码和我们相同，恒等 | upstream writes saves with the same code as ours, so this is the identity
static int convert_step(const char *files, dm_pkg_view *v) { (void)files; if (!strcmp(v->source_format, "d2r-switch-plus") || !strcmp(v->source_format, "d2r-mobile") || v->converted_from[0]) return DM_PKG_OK; LOG("存档管理：来源格式 %s 需要转换，这个版本还不支持", v->source_format); return DM_PKG_E_CONVERT; }

// ---- 本机角色（按客体名；ASCII 大小写不同算同名，别的折叠由文件系统探测）| Local characters (by guest name; ASCII case differences count as the same name, other folding is probed on the file system)
// Settings 既是设置目录，又是合法角色名：角色只占 Settings.<角色扩展名> 这些文件，覆盖时不动目录里的设置 | Settings is both the settings directory and a legal character name: the character owns only Settings.<character extension>; overwriting leaves the settings in the directory alone
static int reserved_dir(const char *n) { return ieq_ascii(n, "Settings"); }
static int local_char(const char *save, const char *name, char *guest_out, size_t gn, char *phys_out, size_t pn) {
    DIR *d = opendir(save); if (!d) return 0; struct dirent *e; int hit = 0; while (!hit && (e = readdir(d))) { if (e->d_name[0] == '.' || e->d_name[0] == '_') continue; char g[256]; guest_of(save, e->d_name, g, sizeof g); if (!ieq_ascii(g, name)) continue; pth p; snprintf(p, sizeof p, "%s/%s", save, e->d_name); struct stat st; if (stat(p, &st) || !S_ISDIR(st.st_mode)) continue; strlcpy(guest_out, g, gn); strlcpy(phys_out, e->d_name, pn); hit = 1; } closedir(d);
    if (!hit) { pth p; struct stat st; snprintf(p, sizeof p, "%s/%s", save, name); if (!stat(p, &st) && S_ISDIR(st.st_mode)) { strlcpy(guest_out, name, gn); strlcpy(phys_out, name, pn); hit = 1; } }   // 字节不同但文件系统说是同一个（APFS 的规范化不敏感）| different bytes, but the file system says it is the same (APFS is normalization-insensitive)
    if (hit && reserved_dir(guest_out)) { pth p; struct stat st; snprintf(p, sizeof p, "%s/%s/%s.d2s", save, phys_out, guest_out); if (stat(p, &st)) hit = 0; }
    return hit;
}
// 同包重名：只差 ASCII 大小写，或名字带组合记号 / 组合用谚文（NFD 写法）。NFC / NFD 是否算同一个由文件系统决定，所以带这种写法的一律拒绝，三端结果才相同；解压之前判，什么都不写
// Same names in one package: differing only in ASCII case, or a name with combining marks / conjoining Hangul (NFD form). Whether NFC and NFD coincide depends on the file system, so such names are refused outright and all platforms agree; judged before extraction, nothing is written
static int name_nfd(const char *s) { for (const uint8_t *p = (const uint8_t *)s; *p; p++) { if (p[0] >= 0xcc && p[0] <= 0xcd && p[1]) { uint32_t u = (uint32_t)(p[0] & 0x1f) << 6 | (p[1] & 0x3f); if (u >= 0x300 && u <= 0x36f) return 1; } if (p[0] == 0xe1 && p[1] >= 0x84 && p[1] <= 0x87) return 1; } return 0; }
static int names_clash(const jn *chars) { if (!chars || chars->t != J_ARR) return 0; for (const jn *a = chars->kid; a; a = a->next) { const char *x = jstrv(a, "name"); if (name_nfd(x)) return 1; for (const jn *b = a->next; b; b = b->next) if (ieq_ascii(x, jstrv(b, "name"))) return 1; } return 0; }
static void stage_paths(const char *save, char *tmp, char *pend) { snprintf(tmp, 1400, "%s.import.tmp", save); snprintf(pend, 1400, "%s.import", save); }
void dm_savepkg_view_free(dm_pkg_view *v) { free(v->chars); v->chars = NULL; }
void dm_savepkg_stage_cancel(const char *save) { pth t, p; stage_paths(save, t, p); rm_tree(t); }

// ---- 上游 D2R Mobile 存档（交接/RC2_原版存档导入_可行性与设计_20261031.md）：改成包的形状后走现有校验、预览、备份、换入；只读来源
// Upstream D2R Mobile saves: shaped into a package, then the existing validation, preview, backup and apply take over; the source is only read
static int has_saves(const char *dir) {
    DIR *d = opendir(dir); if (!d) return 0; struct dirent *e; int found = 0;
    while (!found && (e = readdir(d))) { if (e->d_name[0] == '.' || e->d_name[0] == '_') continue; pth p; struct stat s; snprintf(p, sizeof p, "%s/%s/%s.d2s", dir, e->d_name, e->d_name); if (!lstat(p, &s) && S_ISREG(s.st_mode)) { found = 1; break; }
        char st[256]; strlcpy(st, e->d_name, sizeof st); char *dot = strrchr(st, '.'); if (dot && !strcmp(dot, ".d2i")) { *dot = 0; found = is_stash_name(st); } else if (is_stash_name(st)) { snprintf(p, sizeof p, "%s/%s/%s.d2i", dir, st, st); found = !lstat(p, &s) && S_ISREG(s.st_mode); } }
    closedir(d); return found;
}
// 选的可能是存档根，也可能是它的上级（App 的 Documents、拷出来的 files）；只认叫 save 的那一个，save.test、save.work、联机根都不进 | the pick may be the save root or a parent of it; only the one named save is entered, never save.test, save.work or the co-op root
static int foreign_root(const char *in, char *root, size_t n) {
    static const char *const k[] = { "", "/save", "/Documents/save", "/files/save" };
    for (size_t i = 0; i < sizeof k / sizeof *k; i++) { pth p; snprintf(p, sizeof p, "%s%s", in, k[i]); if (has_saves(p)) { strlcpy(root, p, n); return 1; } }
    return 0;
}
static int foreign_zip(zf *z, const char *tree) {
    char prefix[512] = ""; int have = 0;
    for (int i = 0; i < z->n; i++) { const char *nm = z->e[i].name; if (strstr(nm, "..") || nm[0] == '/' || strchr(nm, '\\')) return DM_PKG_E_PATH; if (coop_path(nm)) { LOG("存档管理：包里有联机角色标记，整包拒绝"); return DM_PKG_E_COOP; } }
    for (int i = 0; i < z->n; i++) { const char *nm = z->e[i].name; if (zip_junk(nm)) continue; size_t L = strlen(nm); int up = L > 4 && !strcmp(nm + L - 4, ".d2s") ? 2 : L > 4 && !strcmp(nm + L - 4, ".d2i") ? 1 : 0; if (!up) continue; char t[512]; strlcpy(t, nm, sizeof t); char *sl = strrchr(t, '/'); if (!sl && up == 2) continue; if (sl) *sl = 0; else t[0] = 0;
        if (up == 1 && t[0]) { const char *par = strrchr(t, '/'); par = par ? par + 1 : t; if (is_stash_name(par)) up = 2; } if (up == 2) { char *s2 = strrchr(t, '/'); if (s2) *s2 = 0; else t[0] = 0; } if (t[0]) strlcat(t, "/", sizeof t); if (!have || strlen(t) < strlen(prefix)) { strlcpy(prefix, t, sizeof prefix); have = 1; } }
    if (!have) return DM_PKG_E_NOTOURS;
    size_t pl = strlen(prefix); int cnt = 0; long long total = 0;
    for (int i = 0; i < z->n; i++) { zr *e = &z->e[i]; if (zip_junk(e->name) || strncmp(e->name, prefix, pl)) continue; const char *rem = e->name + pl; char unit[64]; int c = dm_savepkg_path_class(rem, unit, sizeof unit); if (c != PC_CHAR && c != PC_STASH) continue;
        if (++cnt > MAX_FILES || (total += e->usize) > MAX_TOTAL) return DM_PKG_E_LIMIT; int err = 0; uint8_t *d = zf_read(z, e, MAX_FILE, &err); if (!d) return err ? err : DM_PKG_E_IO; pth t; snprintf(t, sizeof t, "%s/%s", tree, rem); struct stat ts; if (!lstat(t, &ts)) { free(d); LOG("存档管理：包里有同名角色，整包拒绝"); return DM_PKG_E_DUP; } int bad = mkdirs_for(t) || write_new(t, d, e->usize); free(d); if (bad) return DM_PKG_E_IO; }
    return cnt ? DM_PKG_OK : DM_PKG_E_NOTOURS;
}
int dm_savepkg_wrap_foreign(const char *in, const char *work, char *out, size_t n, int *partial) {
    *partial = 0; strlcpy(out, in, n); struct stat st; if (stat(in, &st)) return DM_PKG_E_IO; if (coop_root(in)) { LOG("存档管理：联机存档根不能导入"); return DM_PKG_E_COOP; }
    pth root, tree; snprintf(tree, sizeof tree, "%s/tree", work); size_t L = strlen(in); int rc = DM_PKG_OK;
    if (S_ISDIR(st.st_mode)) { if (!foreign_root(in, root, sizeof root)) return DM_PKG_E_NOTOURS; rm_tree(work); }
    else if (L > 4 && (!strcasecmp(in + L - 4, ".d2s") || !strcasecmp(in + L - 4, ".d2i"))) { rm_tree(work); strlcpy(root, tree, sizeof root); size_t bn = 0; uint8_t *b = read_all(in, MAX_FILE, &bn); if (!b) return DM_PKG_E_IO; pth t; const char *base = strrchr(in, '/'); base = base ? base + 1 : in;
        if (!strcasecmp(in + L - 4, ".d2s")) { dm_pkg_char c; memset(&c, 0, sizeof c); if (!dm_savepkg_parse_d2s(b, bn, &c) || !dm_savepkg_name_ok(c.name)) { free(b); return DM_PKG_E_NOTOURS; } snprintf(t, sizeof t, "%s/%s/%s.d2s", tree, c.name, c.name); }
        else { char stem[256]; strlcpy(stem, base, sizeof stem); stem[strlen(stem) - 4] = 0; if (!is_stash_name(stem) || bn < 4 || rd32(b) != 0xaa55aa55) { free(b); return DM_PKG_E_NOTOURS; } snprintf(t, sizeof t, "%s/%s.d2i", tree, stem); }
        *partial = 1; rc = mkdirs_for(t) || write_new(t, b, bn) ? DM_PKG_E_IO : DM_PKG_OK; free(b); if (rc) return rc; }
    else { zf z; if (zf_open(&z, in)) { zf_close(&z); return DM_PKG_OK; } if (zf_find(&z, "manifest.json")) { zf_close(&z); return DM_PKG_OK; } rm_tree(work); strlcpy(root, tree, sizeof root); mkdir(work, 0755); mkdir(tree, 0755); rc = foreign_zip(&z, tree); zf_close(&z); if (rc) return rc; }
    mkdir(work, 0755); pth zip; snprintf(zip, sizeof zip, "%s/upstream.zip", work); dm_pkg_export_opts o; memset(&o, 0, sizeof o); o.nnames = -1; o.stash = 1; o.kind = "export"; o.now = (int64_t)time(NULL); o.source_format = "d2r-mobile"; static const dm_pkg_source src = { "", "", "", "", "" };
    rc = dm_savepkg_export(root, zip, &o, &src); if (!rc) strlcpy(out, zip, n); return rc;
}
int dm_savepkg_inspect(const char *zip, const char *save, int restore, dm_pkg_view *v) {
    memset(v, 0, sizeof *v); if (coop_root(save)) { LOG("存档管理：联机存档根不能作为导入目标"); return DM_PKG_E_COOP; } pth tmp, pend, failed; stage_paths(save, tmp, pend); snprintf(failed, sizeof failed, "%s.import.failed", save); rm_tree(tmp); rm_tree(failed);
    zf z; int rc = zf_open(&z, zip); jn *m = NULL; uint8_t *mj = NULL; v->chars = calloc(DM_PKG_MAX_CHARS, sizeof *v->chars); if (!v->chars) rc = DM_PKG_E_IO; if (rc) goto done;
    for (int i = 0; i < z.n; i++) if (coop_path(z.e[i].name)) { LOG("存档管理：包里有联机角色标记，整包拒绝"); rc = DM_PKG_E_COOP; goto done; }
    const zr *me = zf_find(&z, "manifest.json"); if (!me) { rc = DM_PKG_E_NOTOURS; goto done; } int zerr = 0; mj = zf_read(&z, me, MAX_MANIFEST, &zerr); if (!mj) { rc = zerr; goto done; }
    const char *mp = (const char *)mj; m = jval(&mp, 0); if (!m || m->t != J_OBJ) { rc = DM_PKG_E_NOTOURS; goto done; } ws(&mp); if (*mp) { rc = DM_PKG_E_NOTOURS; goto done; } if (strcmp(jstrv(m, "format"), "d2rmp-saves")) { rc = DM_PKG_E_NOTOURS; goto done; }
    v->format_major = (int)jnum(m, "format_major", 0); v->format_minor = (int)jnum(m, "format_minor", 0); v->save_gen = (int)jnum(m, "save_gen", 0); if (v->format_major < 1) { rc = DM_PKG_E_NOTOURS; goto done; } if (v->format_major > DM_PKG_FORMAT_MAJOR || v->save_gen > DM_PKG_SAVE_GEN) { rc = DM_PKG_E_TOONEW; goto done; }
    { int cp = jflag(m, "coop"); jn *cl = jget(m, "characters"); if (cl && cl->t == J_ARR) for (jn *c = cl->kid; c; c = c->next) cp |= jflag(c, "coop"); jn *fl = jget(m, "files"); if (fl && fl->t == J_ARR) for (jn *c = fl->kid; c; c = c->next) cp |= coop_path(jstrv(c, "path")); if (cp) { LOG("存档管理：manifest 标了联机角色，整包拒绝"); rc = DM_PKG_E_COOP; goto done; } }
    jn *src = jget(m, "source"); strlcpy(v->platform, jstrv(src, "platform"), sizeof v->platform); strlcpy(v->commit, jstrv(src, "commit"), sizeof v->commit); strlcpy(v->build, jstrv(src, "build"), sizeof v->build); strlcpy(v->version, jstrv(src, "version"), sizeof v->version); strlcpy(v->game_build, jstrv(m, "game_build"), sizeof v->game_build); strlcpy(v->exported_at, jstrv(m, "exported_at"), sizeof v->exported_at); strlcpy(v->kind, jstrv(m, "kind"), sizeof v->kind);
    strlcpy(v->source_format, jget(m, "source_format") ? jstrv(m, "source_format") : "d2r-switch-plus", sizeof v->source_format); if (!v->source_format[0]) strlcpy(v->source_format, "unknown", sizeof v->source_format); strlcpy(v->source_version, jstrv(m, "source_version"), sizeof v->source_version); strlcpy(v->converted_from, jstrv(m, "converted_from"), sizeof v->converted_from);   // converted_from 为 null 时 jstrv 给空串 | jstrv gives "" when converted_from is null
    jn *files = jget(m, "files"); if (!files || files->t != J_ARR) { rc = DM_PKG_E_NOTOURS; goto done; }
    // 先验结构与上限（什么都不写）：每条路径、单文件、总量、条数；ZIP 里多出来的只放过杂项 | structure and limits first (nothing written yet): each path, each file, the total, the count; extras in the ZIP pass only if they are harmless leftovers
    int cnt = 0; long long total = 0; for (jn *f = files->kid; f; f = f->next, cnt++) { const char *p = jstrv(f, "path"); long long sz = jnum(f, "size", -1); char unit[64]; int c = dm_savepkg_path_class(p, unit, sizeof unit); if (c == PC_UNSAFE) { rc = DM_PKG_E_PATH; goto done; } const zr *e = zf_find(&z, p); if (!e || sz < 0 || (uint64_t)sz != e->usize) { rc = DM_PKG_E_CHECK; goto done; } if (sz > MAX_FILE) { rc = DM_PKG_E_LIMIT; goto done; } total += sz; if (strlen(jstrv(f, "sha256")) != 64) { rc = DM_PKG_E_CHECK; goto done; } }
    if (cnt > MAX_FILES || total > MAX_TOTAL) { rc = DM_PKG_E_LIMIT; goto done; }
    for (int i = 0; i < z.n; i++) { const char *n = z.e[i].name; if (zip_traverses(n)) { rc = DM_PKG_E_PATH; goto done; } if (!strcmp(n, "manifest.json") || zip_junk(n)) continue; int in = 0; for (jn *f = files->kid; f && !in; f = f->next) in = !strcmp(jstrv(f, "path"), n); if (in) continue; char unit[64]; rc = dm_savepkg_path_class(n, unit, sizeof unit) == PC_UNSAFE ? DM_PKG_E_PATH : DM_PKG_E_CHECK; goto done; }
    if (names_clash(jget(m, "characters"))) { LOG("存档管理：包里有同名角色，整包拒绝"); rc = DM_PKG_E_DUP; goto done; }
    long long fr = dm_savepkg_free_bytes(save); pth parent; strlcpy(parent, save, sizeof parent); if (fr < 0) { char *sl = strrchr(parent, '/'); if (sl) *sl = 0; fr = dm_savepkg_free_bytes(parent); } if (fr >= 0 && fr < 2 * total + (32ll << 20)) { rc = DM_PKG_E_SPACE; goto done; } if (test_fail("space")) { rc = DM_PKG_E_SPACE; goto done; }
    // 解到临时目录，逐文件 SHA-256 | extract into the temporary directory, SHA-256 per file
    if (mkdirs_for(tmp) || mkdir(tmp, 0755)) { rc = DM_PKG_E_IO; goto done; } pth fd; snprintf(fd, sizeof fd, "%s/files", tmp); if (mkdir(fd, 0755)) { rc = DM_PKG_E_IO; goto done; }
    int pn = 0, pi = 0; for (jn *f = files->kid; f; f = f->next) pn++;
    for (jn *f = files->kid; f; f = f->next) { prog(++pi, pn); const char *p = jstrv(f, "path"); char unit[64] = ""; int c = dm_savepkg_path_class(p, unit, sizeof unit); if (c == PC_UNKNOWN) { v->skipped_unknown++; continue; } const zr *e = zf_find(&z, p); uint8_t *b = zf_read(&z, e, MAX_FILE, &zerr); if (!b) { rc = zerr; goto done; } char h[65]; sha_hex(b, e->usize, h); if (strcmp(h, jstrv(f, "sha256"))) { free(b); rc = DM_PKG_E_CHECK; goto done; }
        pth dst; snprintf(dst, sizeof dst, "%s/%s", fd, p); int wr = mkdirs_for(dst); if (!wr) wr = write_new(dst, b, e->usize); int wer = errno; free(b); if (wr) { rc = wer == EEXIST ? DM_PKG_E_DUP : DM_PKG_E_IO; goto done; } v->nfiles++; v->total_bytes += e->usize; if (c == PC_SETTINGS) v->has_settings = 1; }   // EEXIST：同一个包里两个名字落到同一个文件（大小写、NFC / NFD）| EEXIST: two names in one package land on the same file (case, NFC / NFD)
    rc = convert_step(fd, v); if (rc) goto done;
    pfl l = {0}; if (walk(fd, &l)) { free(l.v); rc = DM_PKG_E_IO; goto done; } for (int i = 0; i < l.n; i++) if (hash_file(&l.v[i])) { free(l.v); rc = DM_PKG_E_IO; goto done; }
    // 角色行：字段以实际解析为准，manifest 对不上就是校验不符 | character rows: fields come from what we parse; a manifest that disagrees is a checksum failure
    jn *chars = jget(m, "characters"); if (chars && chars->t == J_ARR) for (jn *c = chars->kid; c; c = c->next) { if (v->nchars >= DM_PKG_MAX_CHARS) { free(l.v); rc = DM_PKG_E_LIMIT; goto done; } const char *nm = jstrv(c, "name"); dm_pkg_char *x = &v->chars[v->nchars++]; strlcpy(x->name, nm, sizeof x->name); x->status = DM_PKG_BAD; x->elysium = (int)jnum(c, "elysium", 0); x->topup = (int)jnum(c, "stash_topup", 0);
        if (!dm_savepkg_name_ok(nm)) continue; pth dp; snprintf(dp, sizeof dp, "%s/%s/%s.d2s", fd, nm, nm); size_t bn = 0; uint8_t *b = read_all(dp, MAX_FILE, &bn); dm_pkg_char pc; memset(&pc, 0, sizeof pc); int ok = b && dm_savepkg_parse_d2s(b, bn, &pc) && !strcmp(pc.name, nm); free(b); if (!ok) continue;
        if (jnum(c, "valid", 1) && (jnum(c, "class", -1) != pc.cls || jnum(c, "level", -1) != pc.level || jnum(c, "hardcore", -1) != pc.hardcore || (int64_t)jnum(c, "last_played", -1) != (int64_t)pc.last_played)) { free(l.v); rc = DM_PKG_E_CHECK; goto done; } char us[65]; unit_sha(&l, nm, us); if (strcmp(us, jstrv(c, "unit_sha256"))) { free(l.v); rc = DM_PKG_E_CHECK; goto done; }
        x->cls = pc.cls; x->level = pc.level; x->progress = pc.progress; x->hardcore = pc.hardcore; x->expansion = pc.expansion; x->last_played = pc.last_played;
        pth cp; snprintf(cp, sizeof cp, "%s/%s/%s.swcharm", fd, nm, nm); struct stat cs; if (!lstat(cp, &cs)) { size_t cn = 0; uint8_t *cb = read_all(cp, MAX_FILE, &cn); int cok = cb && dm_savepkg_swcharm_ok(cb, cn); free(cb); if (!cok) { LOG("存档管理：护身符页文件头不对，这个角色无法导入"); continue; } }   // 头坏不能当空页 | a bad header must not pass as an empty page
        char lg[64], lp[256]; if (local_char(save, nm, lg, sizeof lg, lp, sizeof lp) && dm_savepkg_is_coop_char(save, lp)) { strlcpy(x->local, lg, sizeof x->local); continue; }   // 同名的是本机联机角色：不能覆盖，这一行保持「无法导入」| the same name is a local co-op character: it must not be overwritten, this row stays "cannot import"
        if (local_char(save, nm, lg, sizeof lg, lp, sizeof lp)) { strlcpy(x->local, lg, sizeof x->local); pth lroot; snprintf(lroot, sizeof lroot, "%s/%s", save, lp); pfl ll = {0}; DIR *ld = opendir(lroot); struct dirent *le; if (ld) { while ((le = readdir(ld))) { if (le->d_name[0] == '.') continue; pth f2; snprintf(f2, sizeof f2, "%s/%s", lroot, le->d_name); struct stat s2; char g2[256], rel[600]; guest_of(lroot, le->d_name, g2, sizeof g2); snprintf(rel, sizeof rel, "%s/%s", lg, g2); if (!lstat(f2, &s2) && S_ISREG(s2.st_mode)) pf_add(&ll, rel, f2, &s2); } closedir(ld); }
            qsort(ll.v, (size_t)ll.n, sizeof *ll.v, cmp_pf); char ls[65] = ""; int same = 0; if (!strcmp(lg, nm)) { int bad = 0; for (int k = 0; k < ll.n; k++) bad |= hash_file(&ll.v[k]); if (!bad) { unit_sha(&ll, lg, ls); same = !strcmp(ls, us); } } free(ll.v); pth ld2; snprintf(ld2, sizeof ld2, "%s/%s/%s.d2s", save, lp, lg); size_t ln = 0; uint8_t *lb = read_all(ld2, MAX_FILE, &ln); dm_pkg_char lc; memset(&lc, 0, sizeof lc); if (lb && dm_savepkg_parse_d2s(lb, ln, &lc)) { x->local_level = lc.level; x->local_played = lc.last_played; } free(lb); x->status = same ? DM_PKG_SAME : DM_PKG_CONFLICT; x->action = same ? DM_PKG_SKIP : restore ? DM_PKG_OVERWRITE : DM_PKG_SKIP; }
        else { x->status = DM_PKG_NEW; x->action = DM_PKG_OVERWRITE; } }
    jn *st = jget(m, "stash"); if (st && st->t == J_ARR) for (jn *c = st->kid; c && v->nstash < DM_PKG_MAX_STASH; c = c->next) { dm_pkg_stash *x = &v->stash[v->nstash++]; const char *nm = jstrv(c, "name"); strlcpy(x->name, nm, sizeof x->name); char us[65]; unit_sha(&l, nm, us); if (!is_stash_name(nm) || strcmp(us, jstrv(c, "unit_sha256"))) { free(l.v); rc = DM_PKG_E_CHECK; goto done; }
        pfl ll = {0}; int found = 0; if (!walk(save, &ll)) { for (int k = 0; k < ll.n; k++) if (ll.v[k].cls == PC_STASH && !strcmp(ll.v[k].unit, nm)) { found = 1; hash_file(&ll.v[k]); } char ls[65]; if (found) { pfl only = {0}; only.v = ll.v; only.n = ll.n; unit_sha(&only, nm, ls); x->status = !strcmp(ls, us) ? DM_PKG_SAME : DM_PKG_CONFLICT; } else x->status = DM_PKG_NEW; } else x->status = DM_PKG_NEW; free(ll.v); x->action = x->status == DM_PKG_NEW ? DM_PKG_OVERWRITE : x->status == DM_PKG_CONFLICT && restore ? DM_PKG_OVERWRITE : DM_PKG_SKIP; }
    { jn *cu = jget(m, "custom"); if (cu && cu->t == J_OBJ) for (jn *c = cu->kid; c && v->ncustom < 16; c = c->next) if (custom_ok(c->key) && (c->t == J_NUM || c->t == J_BOOL) && (c->i == 0 || c->i == 1)) { strlcpy(v->custom[v->ncustom][0], c->key, sizeof v->custom[0][0]); snprintf(v->custom[v->ncustom][1], sizeof v->custom[0][1], "%d", (int)c->i); v->ncustom++; } if (v->ncustom) v->has_settings = 1; }   // 只认白名单和 0 / 1；不跟设置一起选就不动 | allow-list and 0 / 1 only; untouched unless settings are selected
    free(l.v); v->settings_action = restore && v->has_settings ? DM_PKG_OVERWRITE : DM_PKG_SKIP;
    { size_t mn = strlen((char *)mj); pth mf; snprintf(mf, sizeof mf, "%s/manifest.json", tmp); if (write_new(mf, mj, mn)) { rc = DM_PKG_E_IO; goto done; } }
    LOG("存档管理：校验通过（%d 个角色、%d 类共享仓库、%d 个文件，跳过未知文件 %d 个，来源 %s）", v->nchars, v->nstash, v->nfiles, v->skipped_unknown, v->platform);
done:
    if (rc) LOG("存档管理：校验失败 代码 %d", rc);
    if (m) jfree(m); free(mj); zf_close(&z); if (rc) { rm_tree(tmp); } return rc;
}

// ---- 待应用 | The pending directory
static int plan_write(const char *tmp, const dm_pkg_view *v, const char *const *custom_kv) {
    char *b = NULL; size_t n = 0; FILE *f = open_memstream(&b, &n); if (!f) return -1; uint8_t r[8]; int ur = open("/dev/urandom", O_RDONLY); if (ur >= 0) { if (read(ur, r, 8) != 8) memset(r, 7, 8); close(ur); } char id[17]; hex(r, 8, id); fprintf(f, "id\t%s\n", id);
    for (int i = 0; i < v->nchars; i++) { const dm_pkg_char *c = &v->chars[i]; if (c->status == DM_PKG_BAD || c->status == DM_PKG_SAME || c->action == DM_PKG_SKIP) continue; fprintf(f, "char\t%s\t%s\t%s\t%s\t%d\t%d\n", c->name, c->action == DM_PKG_KEEPBOTH ? "k" : "o", c->action == DM_PKG_KEEPBOTH ? c->newname : "-", c->action == DM_PKG_OVERWRITE && c->local[0] ? c->local : "-", c->elysium, c->topup); }
    for (int i = 0; i < v->nstash; i++) if (v->stash[i].action == DM_PKG_OVERWRITE) fprintf(f, "stash\t%s\n", v->stash[i].name);
    if (v->settings_action == DM_PKG_OVERWRITE) { fprintf(f, "settings\t1\n"); if (!custom_kv) for (int i = 0; i < v->ncustom; i++) fprintf(f, "custom\t%s\t%s\n", v->custom[i][0], v->custom[i][1]); } if (custom_kv) for (int i = 0; custom_kv[i]; i += 2) fprintf(f, "custom\t%s\t%s\n", custom_kv[i], custom_kv[i + 1]);
    fclose(f); pth p; snprintf(p, sizeof p, "%s/plan.tsv", tmp); int rc = write_new(p, b, n); free(b); return rc;
}
int dm_savepkg_stage_commit(const char *save, const dm_pkg_view *v, const char *const *custom_kv) {
    if (coop_root(save)) return DM_PKG_E_COOP; pth tmp, pend; stage_paths(save, tmp, pend); if (plan_write(tmp, v, custom_kv)) return DM_PKG_E_IO; rm_tree(pend); if (rename(tmp, pend)) return DM_PKG_E_IO; LOG("存档管理：导入已确认，下次启动换入"); return DM_PKG_OK;
}
static int copy_verified(const char *src, const char *dst, int64_t now) {   // 拷一个文件并读回比对；修改时间设成换入时刻（云同步「后写赢」要它是最新的）| copy and read back; mtime = apply time (cloud sync's last-write-wins needs it to be the newest)
    size_t n = 0, m = 0; uint8_t *b = read_all(src, MAX_FILE, &n); if (!b) return -1; if (mkdirs_for(dst)) { free(b); return -1; } unlink(dst); int rc = write_new(dst, b, n); if (!rc) { uint8_t *c = read_all(dst, MAX_FILE, &m); rc = !c || m != n || memcmp(b, c, n) ? -1 : 0; free(c); } free(b); if (!rc) { struct timeval tv[2] = { { (time_t)now, 0 }, { (time_t)now, 0 } }; rc = utimes(dst, tv); } return rc;
}
static int settings_keep_lang(const char *root, const char *imp, int64_t now) {   // 保留本机的语言：换机不改界面语言；本机没有设置文件时沿用包里的。root = 换入前的存档，改的是事务树里刚拷好的那份，待应用目录不动（重试时还要校验）| keep this device's language: moving devices must not change the UI language; with no local settings the package's stands. root = the save before apply; the copy just placed in the transaction tree is edited, the pending directory stays as it is (a retry verifies it again)
    pth lp, ip; snprintf(lp, sizeof lp, "%s/Settings/Settings.json", root); snprintf(ip, sizeof ip, "%s/Settings/Settings.json", imp); size_t ln = 0, in = 0; uint8_t *l = read_all(lp, 1 << 16, &ln), *i = read_all(ip, 1 << 16, &in); int rc = 0; if (!l || !i) { free(l); free(i); return 0; }
    char *out = malloc(in + 64); if (!out) { free(l); free(i); return -1; } static const char *const keys[] = { "\"Language\"", "\"VoiceLanguage\"" }; memcpy(out, i, in + 1);
    for (int k = 0; k < 2; k++) { const char *lk = strstr((char *)l, keys[k]), *lc = lk ? strchr(lk, ':') : NULL; char *ik = strstr(out, keys[k]), *ic = ik ? strchr(ik, ':') : NULL; if (!lc || !ic) continue; int val = atoi(lc + 1); char *ve = ic + 1; while (*ve == ' ') ve++; char *vs = ve; if (*ve == '-') ve++; while (*ve >= '0' && *ve <= '9') ve++; if (ve == vs) continue; char num[16]; int nl = snprintf(num, sizeof num, "%d", val); memmove(vs + nl, ve, strlen(ve) + 1); memcpy(vs, num, (size_t)nl); }
    rc = write_replace(ip, out, strlen(out)); free(out); free(l); free(i); if (!rc) { struct timeval tv[2] = { { (time_t)now, 0 }, { (time_t)now, 0 } }; rc = utimes(ip, tv); } return rc;
}
static void result_write(const char *save, int ok, int chars, int code) { pth p; snprintf(p, sizeof p, "%s.import.result", save); char b[64]; int n = snprintf(b, sizeof b, "%d %d %d\n", ok, chars, code); write_replace(p, b, (size_t)n); }
int dm_savepkg_result_take(const char *save, int *ok, int *chars, int *code) { pth p; snprintf(p, sizeof p, "%s.import.result", save); size_t n = 0; uint8_t *b = read_all(p, 256, &n); if (!b) return 0; int r = sscanf((char *)b, "%d %d %d", ok, chars, code) == 3; free(b); unlink(p); return r; }

static int rm_char(const char *root, const char *phys) {
    pth d; struct stat ds; snprintf(d, sizeof d, "%s/%s", root, phys); if (!reserved_dir(phys)) return rm_tree(d); if (lstat(d, &ds) || !S_ISDIR(ds.st_mode)) { LOG("【红】存档管理：设置目录不是普通目录，这次不换入"); return -1; } DIR *dp = opendir(d); if (!dp) return -1; struct dirent *e; int rc = 0;
    while ((e = readdir(dp))) { if (e->d_name[0] == '.') continue; char rel[600], unit[64]; snprintf(rel, sizeof rel, "%s/%s", phys, e->d_name); if (dm_savepkg_path_class(rel, unit, sizeof unit) != PC_CHAR) continue; pth f; snprintf(f, sizeof f, "%s/%s", d, e->d_name); rc |= unlink(f); } closedir(dp); return rc;
}
static int apply_char(const char *fd, const char *imp, const char *name, const char *act, const char *newname, const char *local, int ely, int top, int64_t now, FILE *fu) {
    const char *dn = !strcmp(act, "k") ? newname : name; char lg[64], lp[256];
    if (!strcmp(act, "k") && local_char(imp, dn, lg, sizeof lg, lp, sizeof lp)) { LOG("【红】存档管理：保留两份的新名已被占用，这次不换入"); return -1; }   // 预览之后才出现的：普通角色和联机角色都不能并进去 | appeared after the preview: neither a normal nor a co-op character may be merged into
    if (local_char(imp, !strcmp(local, "-") ? name : local, lg, sizeof lg, lp, sizeof lp) && dm_savepkg_is_coop_char(imp, lp) && strcmp(act, "k")) { LOG("存档管理：同名的是联机角色，这一个不换入"); return 0; }   // 预览之后才变成联机角色也不覆盖 | also holds if it became a co-op character after the preview
    if (!strcmp(local, "-")) { if (!strcmp(act, "o") && local_char(imp, name, lg, sizeof lg, lp, sizeof lp)) { if (rm_char(imp, lp)) return -1; } } else if (local_char(imp, local, lg, sizeof lg, lp, sizeof lp)) { if (rm_char(imp, lp)) return -1; list_set(imp, "sw_elysium.txt", lg, 0); list_set(imp, "dm_stash_topup.txt", lg, 0); }
    pth sd, td; snprintf(sd, sizeof sd, "%s/%s", fd, name); snprintf(td, sizeof td, "%s/%s", imp, dn); if (mkdir(td, 0755)) { struct stat ts; if (!(errno == EEXIST && reserved_dir(dn) && !lstat(td, &ts) && S_ISDIR(ts.st_mode))) { LOG("【红】存档管理：角色目录已被占用，这次不换入 errno=%d", errno); return -1; } } DIR *d = opendir(sd); if (!d) return -1; struct dirent *e; int rc = 0; size_t nl = strlen(name);
    while (!rc && (e = readdir(d))) { if (e->d_name[0] == '.') continue; pth s, t; snprintf(s, sizeof s, "%s/%s", sd, e->d_name); if (strncmp(e->d_name, name, nl)) { rc = -1; break; } snprintf(t, sizeof t, "%s/%s%s", td, dn, e->d_name + nl); rc = copy_verified(s, t, now);
        if (!rc && !strcmp(act, "k") && !strcmp(e->d_name + nl, ".d2s")) { size_t n = 0; uint8_t *b = read_all(t, MAX_FILE, &n); if (!b || !dm_savepkg_rename_d2s(b, n, dn) || write_replace(t, b, n)) rc = -1; free(b); if (!rc) { struct timeval tv[2] = { { (time_t)now, 0 }, { (time_t)now, 0 } }; rc = utimes(t, tv); } } }
    closedir(d); if (rc) return rc; rc = list_set(imp, "sw_elysium.txt", dn, ely) | list_set(imp, "dm_stash_topup.txt", dn, top); fprintf(fu, "%s\n", dn); return rc;
}
static int apply_stash(const char *fd, const char *imp, const char *name, int64_t now, FILE *fu) {
    pth d; snprintf(d, sizeof d, "%s/%s", imp, name); if (rm_tree(d)) return -1; for (int k = 0; k < 2; k++) { pth r; snprintf(r, sizeof r, "%s/%s.%s", imp, name, k ? "swvault" : "d2i"); if (unlink(r) && errno != ENOENT) return -1; }
    pfl l = {0}; if (walk(fd, &l)) { free(l.v); return -1; } int rc = 0; for (int i = 0; i < l.n && !rc; i++) if (l.v[i].cls == PC_STASH && !strcmp(l.v[i].unit, name)) { pth t; snprintf(t, sizeof t, "%s/%s", imp, l.v[i].rel); rc = copy_verified(l.v[i].phys, t, now); } free(l.v); (void)fu; return rc;
}
// 换入前按 manifest 重新核一遍待应用的文件：预览之后、重启之前坏掉的不进存档 | before apply, check the pending files against the manifest again: what got damaged between the preview and the restart stays out of the save
static int pend_verify(const char *pend) {
    pth mp; snprintf(mp, sizeof mp, "%s/manifest.json", pend); size_t n = 0; uint8_t *mj = read_all(mp, MAX_MANIFEST, &n); if (!mj) return -1; const char *q = (const char *)mj; jn *m = jval(&q, 0); jn *fl = jget(m, "files"); int rc = fl && fl->t == J_ARR ? 0 : -1;
    if (!rc) for (jn *f = fl->kid; f && !rc; f = f->next) { const char *p = jstrv(f, "path"); char unit[64]; if (dm_savepkg_path_class(p, unit, sizeof unit) <= 0) continue; pth fp; snprintf(fp, sizeof fp, "%s/files/%s", pend, p); size_t bn = 0; uint8_t *b = read_all(fp, MAX_FILE, &bn); if (!b) { rc = -1; break; } char h[65]; sha_hex(b, bn, h); free(b); if (strcmp(h, jstrv(f, "sha256"))) rc = -1; }
    jfree(m); free(mj); return rc;
}
int dm_savepkg_apply_pending(const char *save, const char *custom_dir, int64_t now, const dm_pkg_source *src) {
    pth tmp, pend, imp, done_f, fu_p, att, plan; stage_paths(save, tmp, pend); snprintf(imp, sizeof imp, "%s.imp", save); snprintf(done_f, sizeof done_f, "%s/_import_done", save); snprintf(fu_p, sizeof fu_p, "%s.cloudstate.forceup", save); snprintf(att, sizeof att, "%s/attempts", pend); snprintf(plan, sizeof plan, "%s/plan.tsv", pend);
    if (coop_root(save)) { struct stat cs; if (lstat(pend, &cs)) return 0; pth f; snprintf(f, sizeof f, "%s.import.failed", save); rm_tree(f); rename(pend, f); result_write(save, 0, 0, DM_PKG_E_COOP); LOG("【红】存档管理：联机存档根不换入，已放弃"); return -1; }
    rm_tree(tmp); mkdir(save, 0755); size_t pn = 0; uint8_t *pb = read_all(plan, MAX_MANIFEST, &pn); if (!pb) { struct stat st; if (!lstat(pend, &st)) { LOG("【红】存档管理：待应用目录缺计划，丢弃"); rm_tree(pend); } rm_tree(imp); unlink(done_f); return 0; }
    char *dup = strdup((char *)pb); char id[32] = ""; for (char *s = tok(dup, "\n"); s; s = tok(NULL, "\n")) if (!strncmp(s, "id\t", 3)) { strlcpy(id, s + 3, sizeof id); break; } free(dup); int count = 0; int custom_n = 0; const char *custom_kv[40]; char *custom_buf = NULL; int ret = -1, code = DM_PKG_E_IO;
    size_t dn = 0; uint8_t *db = read_all(done_f, 64, &dn); int already = db && !strcmp((char *)db, id); free(db);
    if (!already) {
        size_t an = 0; uint8_t *ab = read_all(att, 16, &an); int tries = ab ? atoi((char *)ab) : 0; free(ab); if (tries >= 2) { pth f; snprintf(f, sizeof f, "%s.import.failed", save); rm_tree(f); rename(pend, f); rm_tree(imp); result_write(save, 0, 0, DM_PKG_E_IO); LOG("【红】存档管理：换入已失败 %d 次，放弃（存档不变）", tries); free(pb); return -1; }
        char tb[16]; int tn = snprintf(tb, sizeof tb, "%d", tries + 1); write_replace(att, tb, (size_t)tn); test_kill("before_backup");
        if (pend_verify(pend)) { LOG("【红】存档管理：待应用的文件与 manifest 不符，这次不换入（存档不变）"); goto fail; }
        if (test_fail("backup") || dm_savepkg_backup(save, "auto", now, custom_dir, src, NULL, 0)) { LOG("【红】存档管理：换入前自动备份失败，这次不换入（存档不变）"); goto fail; } test_kill("after_backup");
        rm_tree(imp); if (test_fail("clone") || clonefile(save, imp, 0)) { LOG("【红】存档管理：克隆已提交目录失败 errno=%d", errno); goto fail; }
        pth fd; snprintf(fd, sizeof fd, "%s/files", pend); pth fun; snprintf(fun, sizeof fun, "%s/forceup", pend); FILE *fu = fopen(fun, "w"); if (!fu) goto fail; int rc = 0; char *work = strdup((char *)pb);
        for (char *s = work, *nl; !rc && s && *s; s = nl) { nl = strchr(s, '\n'); if (nl) *nl++ = 0; char *f[8]; int k = 0; for (char *t = s; k < 8; ) { f[k++] = t; t = strchr(t, '\t'); if (!t) break; *t++ = 0; }
            if (!strcmp(f[0], "char") && k >= 7) { rc = apply_char(fd, imp, f[1], f[2], f[3], f[4], atoi(f[5]), atoi(f[6]), now, fu); count++; }
            else if (!strcmp(f[0], "stash") && k >= 2) { rc = apply_stash(fd, imp, f[1], now, fu); fprintf(fu, "_stash\n"); }
            else if (!strcmp(f[0], "settings")) { pth sd; struct stat ss; snprintf(sd, sizeof sd, "%s/Settings", imp); if (!lstat(sd, &ss) && !S_ISDIR(ss.st_mode)) { LOG("【红】存档管理：设置目录不是普通目录，这次不换入"); rc = -1; } pfl l = {0}; if (!rc && !walk(fd, &l)) { for (int i = 0; i < l.n && !rc; i++) if (l.v[i].cls == PC_SETTINGS) { pth t; snprintf(t, sizeof t, "%s/%s", imp, l.v[i].rel); rc = copy_verified(l.v[i].phys, t, now); } if (!rc && settings_keep_lang(save, imp, now)) rc = -1; } else rc = -1; free(l.v); } }
        free(work); fclose(fu); test_kill("mid_apply"); if (rc || test_fail("apply")) { LOG("【红】存档管理：按计划写入失败 errno=%d", errno); goto fail; }
        pth imdone; snprintf(imdone, sizeof imdone, "%s/_import_done", imp); unlink(imdone); if (write_new(imdone, id, strlen(id))) goto fail; test_kill("before_swap");
        if (test_fail("swap") || renamex_np(imp, save, RENAME_SWAP)) { LOG("【红】存档管理：原子互换失败 errno=%d", errno); goto fail; } test_kill("after_swap");
    } else { LOG("存档管理：上次换入已完成，只补收尾"); char *work = strdup((char *)pb); for (char *s = tok(work, "\n"); s; s = tok(NULL, "\n")) if (!strncmp(s, "char\t", 5)) count++; free(work); }
    { pth fun; snprintf(fun, sizeof fun, "%s/forceup", pend); size_t n = 0; uint8_t *b = read_all(fun, 1 << 20, &n); if (b) { FILE *f = fopen(fu_p, "a"); if (f) { fwrite(b, 1, n, f); fclose(f); } else LOG("【红】存档管理：云同步标记写失败 errno=%d", errno); free(b); } }   // 互换之后才生效：之前被杀的话，这些单位根本没换入，不能强传 | only effective after the swap: if killed earlier these units were never applied and must not be force-uploaded
    custom_buf = strdup((char *)pb); for (char *s = tok(custom_buf, "\n"); s; s = tok(NULL, "\n")) if (!strncmp(s, "custom\t", 7) && custom_n < 20) { char *k = s + 7, *t = strchr(k, '\t'); if (!t) continue; *t = 0; custom_kv[2 * custom_n] = k; custom_kv[2 * custom_n + 1] = t + 1; custom_n++; }
    if (custom_n && custom_dir && custom_merge(custom_dir, custom_kv, custom_n)) LOG("【红】存档管理：设置项写回失败（存档已换入）");
    free(custom_buf); rm_tree(imp); result_write(save, 1, count, 0); test_kill("before_cleanup"); rm_tree(pend); unlink(done_f); { pth f; snprintf(f, sizeof f, "%s.import.failed", save); rm_tree(f); } LOG("存档管理：换入完成（%d 个角色）", count); free(pb); return 1;
fail:
    (void)code; rm_tree(imp); result_write(save, 0, 0, DM_PKG_E_IO); free(pb); return ret;
}

// ---- 备份：<save>.backups/<yyyyMMdd-HHmmss>[-序号]-<auto|manual>.zip，旁路 <同名>.star = 标星；安卓另存一份到 mirror（同名、同格式、同保留规则）
// ---- Backups: <save>.backups/<yyyyMMdd-HHmmss>[-seq]-<auto|manual>.zip, side file <same name>.star = starred; Android also keeps a copy in the mirror (same name, same format, same retention)
static char g_mirror[1024], g_mirror_old[1024];   // g_mirror 写和读；g_mirror_old 只读（另一个目录名下早先的备份）| g_mirror is written and read; g_mirror_old is read only (earlier backups under the other folder name)
void dm_savepkg_set_mirror(const char *dir) { strlcpy(g_mirror, dir ? dir : "", sizeof g_mirror); g_mirror_old[0] = 0; }
// 安卓备份另存位置（设计稿 §2.1「两个目录名」）：<Download>/S2X/ 下 D2R Mobile Plus 在就用它，否则 D2R Mobile 在就用它，都没有就建 Plus；另一个目录里已有的备份照样列出 | Android's second backup location: D2R Mobile Plus under <Download>/S2X/ if present, else D2R Mobile if present, else create Plus; backups already in the other folder are still listed
int dm_savepkg_mirror_from_downloads(const char *downloads) {
    g_mirror[0] = g_mirror_old[0] = 0; if (!downloads || !downloads[0]) return -1; pth plus, old, p; struct stat st; snprintf(plus, sizeof plus, "%s/S2X/D2R Mobile Plus", downloads); snprintf(old, sizeof old, "%s/S2X/D2R Mobile", downloads);
    int hp = !stat(plus, &st) && S_ISDIR(st.st_mode), ho = !stat(old, &st) && S_ISDIR(st.st_mode); snprintf(p, sizeof p, "%s/SaveBackups/x", hp || !ho ? plus : old); if (mkdirs_for(p)) return -1;
    p[strlen(p) - 2] = 0; strlcpy(g_mirror, p, sizeof g_mirror); snprintf(p, sizeof p, "%s/SaveBackups", hp || !ho ? old : plus); if (!stat(p, &st) && S_ISDIR(st.st_mode)) strlcpy(g_mirror_old, p, sizeof g_mirror_old); return 0;
}
static void backups_dir(const char *save, char *o) { snprintf(o, 1400, "%s.backups", save); }
static int copy_plain(const char *a, const char *b) { size_t n = 0; uint8_t *d = read_all(a, (size_t)MAX_TOTAL * 2, &n); if (!d) return -1; int rc = mkdirs_for(b) ? -1 : write_replace(b, d, n); free(d); return rc; }
int dm_savepkg_backup(const char *save, const char *kind, int64_t now, const char *custom_dir, const dm_pkg_source *src, char *name_out, size_t no) { return dm_savepkg_backup_tree(save, save, kind, now, custom_dir, src, name_out, no); }
int dm_savepkg_backup_tree(const char *save, const char *tree, const char *kind, int64_t now, const char *custom_dir, const dm_pkg_source *src, char *name_out, size_t no) {
    if (coop_root(save) || coop_root(tree)) return DM_PKG_E_COOP; pth dir, p; backups_dir(save, dir); if (mkdir(dir, 0755) && errno != EEXIST) return DM_PKG_E_IO; time_t t = (time_t)now; struct tm tm; localtime_r(&t, &tm); char ts[32]; strftime(ts, sizeof ts, "%Y%m%d-%H%M%S", &tm); char nm[96]; int seq = 0; struct stat st;
    do { if (seq) snprintf(nm, sizeof nm, "%s-%d-%s.zip", ts, seq, kind); else snprintf(nm, sizeof nm, "%s-%s.zip", ts, kind); snprintf(p, sizeof p, "%s/%s", dir, nm); seq++; } while (!lstat(p, &st) && seq < 100);
    dm_pkg_export_opts o; memset(&o, 0, sizeof o); o.nnames = -1; o.stash = 1; o.settings = 1; o.source_root = save; o.custom_dir = custom_dir; o.kind = kind; o.now = now; int rc = dm_savepkg_export(tree, p, &o, src); if (rc) return rc;
    zf z; int zr_ = zf_open(&z, p); if (!zr_) { const zr *m = zf_find(&z, "manifest.json"); if (!m) zr_ = DM_PKG_E_CHECK; else for (int i = 0; i < z.n && !zr_; i++) { int e2 = 0; uint8_t *b = zf_read(&z, &z.e[i], MAX_MANIFEST > MAX_FILE ? MAX_MANIFEST : MAX_FILE, &e2); if (!b) zr_ = e2; free(b); } } zf_close(&z); if (zr_) { unlink(p); LOG("【红】存档管理：备份读回校验失败 代码 %d", zr_); return zr_; }   // 读回校验：坏的备份比没备份更糟 | read-back check: a bad backup is worse than none
    if (g_mirror[0]) { pth mp; snprintf(mp, sizeof mp, "%s/%s", g_mirror, nm); if (copy_plain(p, mp)) LOG("存档管理：外部备份写失败 errno=%d（内部备份已成）", errno); }
    if (name_out) strlcpy(name_out, nm, no); dm_savepkg_backup_prune(save, nm); LOG("存档管理：备份完成（%s）", kind); return DM_PKG_OK;
}
static int is_backup_name(const char *s) { size_t n = strlen(s); return n > 18 && s[0] >= '0' && s[0] <= '9' && !strcmp(s + n - 4, ".zip") && (strstr(s, "-auto.") || strstr(s, "-manual.")); }
static int cmp_name_desc(const void *a, const void *b) { return strcmp(((const dm_pkg_backup *)b)->file, ((const dm_pkg_backup *)a)->file); }
static int backups_scan(const char *dir, int mirror, dm_pkg_backup *out, int n, int cap) {
    DIR *d = opendir(dir); if (!d) return n; struct dirent *e; while (n < cap && (e = readdir(d))) { if (!is_backup_name(e->d_name)) continue; int dup = 0; for (int i = 0; i < n; i++) if (!strcmp(out[i].file, e->d_name)) dup = 1; if (dup) continue; pth p; snprintf(p, sizeof p, "%s/%s", dir, e->d_name); struct stat st; if (lstat(p, &st) || !S_ISREG(st.st_mode)) continue;
        dm_pkg_backup *b = &out[n++]; memset(b, 0, sizeof *b); strlcpy(b->file, e->d_name, sizeof b->file); strlcpy(b->dir, dir, sizeof b->dir); b->size = st.st_size; b->is_auto = strstr(e->d_name, "-auto.") != NULL; b->mirror = mirror; struct tm tm; memset(&tm, 0, sizeof tm); if (sscanf(e->d_name, "%4d%2d%2d-%2d%2d%2d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6) { tm.tm_year -= 1900; tm.tm_mon -= 1; tm.tm_isdst = -1; b->when = mktime(&tm); }
        pth sp; snprintf(sp, sizeof sp, "%s.star", p); b->starred = !lstat(sp, &st); zf z; if (!zf_open(&z, p)) { const zr *m = zf_find(&z, "manifest.json"); int er = 0; uint8_t *mj = m ? zf_read(&z, m, MAX_MANIFEST, &er) : NULL; if (mj) { const char *q = (const char *)mj; jn *j = jval(&q, 0); jn *c = jget(j, "characters"); if (c && c->t == J_ARR) for (jn *k = c->kid; k; k = k->next) b->chars++; jfree(j); free(mj); } } zf_close(&z); }
    closedir(d); return n;
}
int dm_savepkg_backup_list(const char *save, dm_pkg_backup *out, int cap) { pth dir; backups_dir(save, dir); int n = backups_scan(dir, 0, out, 0, cap); if (g_mirror[0]) n = backups_scan(g_mirror, 1, out, n, cap); if (g_mirror_old[0]) n = backups_scan(g_mirror_old, 1, out, n, cap); qsort(out, (size_t)n, sizeof *out, cmp_name_desc); return n; }
int dm_savepkg_backup_star(const char *save, const dm_pkg_backup *b, int on) {
    if (on) { dm_pkg_backup *l = calloc(256, sizeof *l); if (!l) return DM_PKG_E_IO; int n = dm_savepkg_backup_list(save, l, 256), s = 0; for (int i = 0; i < n; i++) s += l[i].starred; free(l); if (s >= MAX_STARRED) return DM_PKG_E_STARFULL; }
    pth dir; backups_dir(save, dir); const char *ds[3] = { dir, g_mirror, g_mirror_old }; int rc = DM_PKG_OK; for (int k = 0; k < 3; k++) { if (!ds[k][0]) continue; pth z, p; snprintf(z, sizeof z, "%s/%s", ds[k], b->file); struct stat st; if (lstat(z, &st)) continue; snprintf(p, sizeof p, "%s.star", z); if (on) { int fd = open(p, O_WRONLY | O_CREAT, 0644); if (fd < 0) rc = DM_PKG_E_IO; else close(fd); } else unlink(p); } return rc;
}
int dm_savepkg_backup_delete(const char *save, const dm_pkg_backup *b) { pth dir; backups_dir(save, dir); const char *ds[3] = { dir, g_mirror, g_mirror_old }; for (int k = 0; k < 3; k++) { if (!ds[k][0]) continue; pth z, p; snprintf(z, sizeof z, "%s/%s", ds[k], b->file); unlink(z); snprintf(p, sizeof p, "%s.star", z); unlink(p); } return DM_PKG_OK; }
static void prune_dir(const char *dir, const char *keep) {
    dm_pkg_backup *l = calloc(1024, sizeof *l); if (!l) return; int n = backups_scan(dir, 0, l, 0, 1024); qsort(l, (size_t)n, sizeof *l, cmp_name_desc); int kept = 0;
    for (int i = 0; i < n; i++) { if (l[i].starred) continue; if (keep && !strcmp(l[i].file, keep)) { kept++; continue; } if (++kept > KEEP_UNSTARRED) { pth z; snprintf(z, sizeof z, "%s/%s", dir, l[i].file); unlink(z); } } free(l);
}
void dm_savepkg_backup_prune(const char *save, const char *keep) { pth dir; backups_dir(save, dir); prune_dir(dir, keep); if (g_mirror[0]) prune_dir(g_mirror, keep); }
