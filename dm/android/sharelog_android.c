// 接口与说明见 sharelog_android.h。| Interface and notes: sharelog_android.h.
#include "sharelog_android.h"
#include <stdint.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

typedef struct { char *p; size_t n, cap; } sb_t;
static int sb_put(sb_t *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) { size_t c = b->cap ? b->cap * 2 : 4096; while (c < b->n + n + 1) c *= 2; char *q = realloc(b->p, c); if (!q) return 0; b->p = q; b->cap = c; }
    memcpy(b->p + b->n, s, n); b->n += n; b->p[b->n] = 0; return 1;
}
static int sb_puts(sb_t *b, const char *s) { return sb_put(b, s, strlen(s)); }
static int is_hex(unsigned char c) { return isxdigit(c) != 0; }
static int is_email_local(unsigned char c) { return isalnum(c) || strchr("._%+-", c) != NULL; }
static int is_email_domain(unsigned char c) { return isalnum(c) || c == '.' || c == '-'; }

int dm_sharelog_names_in_text(const char *t, size_t len, char names[][64], int have, int cap) {
    for (size_t i = 0; i + 6 < len; i++) {
        if (memcmp(t + i, "/save", 5)) continue;
        size_t j = i + 5;
        if (j + 5 <= len && !memcmp(t + j, ".test", 5)) j += 5;
        if (j + 5 <= len && !memcmp(t + j, ".work", 5)) j += 5;
        if (j >= len || t[j] != '/') continue;
        size_t s = ++j; while (j < len && t[j] != '/' && !isspace((unsigned char)t[j])) j++;
        if (j >= len || t[j] != '/' || j == s || j - s >= 64) continue;
        if (j - s == 8 && !memcmp(t + s, "Settings", 8)) continue;
        if (j - s >= 17 && !memcmp(t + s, "ModernSharedStash", 17)) continue;
        int dup = 0; for (int k = 0; k < have && !dup; k++) dup = strlen(names[k]) == j - s && !memcmp(names[k], t + s, j - s);
        if (!dup && have < cap) { memcpy(names[have], t + s, j - s); names[have][j - s] = 0; have++; }
    }
    return have;
}
// 码位是不是「字母 / 数字」（照 iOS 的 \p{L}\p{N}）：ASCII 按 isalnum；多字节里标点 / 符号区段不算（中文标点「」：，、全角标点、通用标点、拉丁 1 标点），其余（汉字、假名、谚文、带重音的拉丁字母、西里尔……）算。
// 原先所有多字节一律当字母，紧挨中文标点的角色名不算整词、没被替换（奥丁导出 2026-10-28 漏 4 处）
// Whether a code point is a letter / number (as iOS's \p{L}\p{N}): ASCII by isalnum; among multi-byte ones the punctuation / symbol blocks are not (CJK punctuation 「」：，、, fullwidth
// punctuation, general punctuation, Latin-1 punctuation) and everything else (Han, kana, Hangul, accented Latin, Cyrillic...) is. All multi-byte characters used to count as letters,
// so a character name right next to CJK punctuation was not a whole word and was not replaced (the Thor export of 2026-10-28 leaked 4)
static int cp_is_word(uint32_t cp) {
    if (cp < 0x80) return cp == '_' || isalnum((int)cp);
    if ((cp >= 0xA0 && cp <= 0xBF) || cp == 0xD7 || cp == 0xF7) return 0;
    if ((cp >= 0x2000 && cp <= 0x206F) || (cp >= 0x3000 && cp <= 0x303F) || (cp >= 0xFE30 && cp <= 0xFE4F)) return 0;
    if ((cp >= 0xFF00 && cp <= 0xFF0F) || (cp >= 0xFF1A && cp <= 0xFF20) || (cp >= 0xFF3B && cp <= 0xFF40) || (cp >= 0xFF5B && cp <= 0xFF65)) return 0;
    return 1;
}
static uint32_t cp_before(const char *t, size_t i) {   // i 之前那个码位 | the code point ending right before i
    size_t s = i - 1; while (s > 0 && ((unsigned char)t[s] & 0xC0) == 0x80 && i - s < 4) s--;
    unsigned char c = (unsigned char)t[s]; uint32_t cp = c < 0x80 ? c : c >= 0xF0 ? c & 7 : c >= 0xE0 ? c & 15 : c & 31;
    for (size_t k = s + 1; k < i; k++) cp = (cp << 6) | ((unsigned char)t[k] & 63); return cp;
}
static uint32_t cp_at(const char *t, size_t len, size_t i) {   // i 处的码位 | the code point starting at i
    unsigned char c = (unsigned char)t[i]; int n = c < 0x80 ? 0 : c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1; uint32_t cp = n == 0 ? c : n == 3 ? c & 7 : n == 2 ? c & 15 : c & 31;
    for (int k = 1; k <= n && i + k < len; k++) cp = (cp << 6) | ((unsigned char)t[i + k] & 63); return cp;
}
// 整词替换（前后都不是字母 / 数字 / 下划线）| whole-word replacement
static char *repl_word(const char *t, size_t len, const char *name, const char *rep, size_t *out_len) {
    size_t nl = strlen(name); sb_t b = {0}; size_t i = 0; if (!nl) { char *c = malloc(len + 1); memcpy(c, t, len); c[len] = 0; *out_len = len; return c; }
    while (i < len) {
        if (i + nl <= len && !memcmp(t + i, name, nl) && (i == 0 || !cp_is_word(cp_before(t, i))) && (i + nl == len || !cp_is_word(cp_at(t, len, i + nl)))) { sb_puts(&b, rep); i += nl; }
        else sb_put(&b, t + i++, 1);
    }
    *out_len = b.n; return b.p ? b.p : calloc(1, 1);
}
static char *repl_lit(const char *t, size_t len, const char *pat, const char *rep, size_t *out_len) {
    size_t pl = strlen(pat); sb_t b = {0}; size_t i = 0; if (pl < 2) { char *c = malloc(len + 1); memcpy(c, t, len); c[len] = 0; *out_len = len; return c; }
    while (i < len) { if (i + pl <= len && !memcmp(t + i, pat, pl)) { sb_puts(&b, rep); i += pl; } else sb_put(&b, t + i++, 1); }
    *out_len = b.n; return b.p ? b.p : calloc(1, 1);
}
// 标识扫描：/Users/<名>、邮箱、UUID（8-4-4-4-12）、8-16 位十六进制、「屏幕：<名字>，」| identifier scan
static char *scan_ids(const char *t, size_t len, size_t *out_len) {
    sb_t b = {0}; size_t i = 0;
    static const unsigned char SCREEN[] = {0xE5, 0xB1, 0x8F, 0xE5, 0xB9, 0x95}, FULLCOLON[] = {0xEF, 0xBC, 0x9A}, FULLCOMMA[] = {0xEF, 0xBC, 0x8C};
    while (i < len) {
        unsigned char c = (unsigned char)t[i];
        if (c == '/' && i + 7 <= len && !memcmp(t + i, "/Users/", 7)) { size_t j = i + 7; while (j < len && t[j] != '/' && !isspace((unsigned char)t[j])) j++; if (j > i + 7) { sb_puts(&b, "<redacted>"); i = j; continue; } }
        if (c == '@') {   // 邮箱：前面 [A-Za-z0-9._%+-]+，后面 [A-Za-z0-9.-]+ 含 . 且以 ≥2 个字母结尾 | e-mail
            size_t s = i; while (s > 0 && is_email_local((unsigned char)t[s - 1])) s--;
            size_t j = i + 1; while (j < len && is_email_domain((unsigned char)t[j])) j++;
            size_t dot = 0; for (size_t k = i + 1; k < j; k++) if (t[k] == '.') dot = k;
            if (s < i && dot && j - dot - 1 >= 2) { int ok = 1; for (size_t k = dot + 1; k < j; k++) if (!isalpha((unsigned char)t[k])) ok = 0;
                if (ok) { if (b.n >= i - s) b.n -= i - s; sb_puts(&b, "<redacted>"); i = j; continue; } }
        }
        if (is_hex(c) && (i == 0 || !is_hex((unsigned char)t[i - 1]))) {   // UUID / 8-16 | UUID / 8-16 hex
            size_t j = i, n = 0; while (j < len && is_hex((unsigned char)t[j])) { j++; n++; }
            if (n == 8 && j < len && t[j] == '-') {
                static const int grp[] = {4, 4, 4, 12}; size_t k = j; int okuuid = 1;
                for (int g = 0; g < 4 && okuuid; g++) { if (k >= len || t[k] != '-') { okuuid = 0; break; } k++; size_t m = 0; while (k < len && is_hex((unsigned char)t[k])) { k++; m++; } if ((int)m != grp[g]) okuuid = 0; }
                if (okuuid && (k == len || !is_hex((unsigned char)t[k]))) { sb_puts(&b, "<redacted>"); i = k; continue; }
                size_t k2 = j + 1, m2 = 0; while (k2 < len && is_hex((unsigned char)t[k2])) { k2++; m2++; }
                if (m2 == 16 && (k2 == len || !is_hex((unsigned char)t[k2]))) { sb_puts(&b, "<redacted>"); i = k2; continue; }
            }
            sb_put(&b, t + i, j - i); i = j; continue;
        }
        if (c == SCREEN[0] && i + 6 <= len && !memcmp(t + i, SCREEN, 6)) {   // 「屏幕」| "screen"
            size_t j = i + 6, s;
            if (j + 3 <= len && !memcmp(t + j, FULLCOLON, 3)) j += 3; else if (j < len && t[j] == ':') j++; else if (j < len && isspace((unsigned char)t[j])) { while (j < len && isspace((unsigned char)t[j]) && t[j] != '\n') j++; } else { sb_put(&b, t + i, 6); i += 6; continue; }
            while (j < len && t[j] == ' ') j++;
            s = j; int digits = 0; while (j < len && isdigit((unsigned char)t[j])) { j++; digits++; }
            if (digits && j < len && t[j] == 'x') { sb_put(&b, t + i, s - i); i = s; continue; }   // 分辨率「屏幕 2622x1206」不动 | resolution: untouched
            size_t e = s, end = 0; while (e < len && t[e] != '\n') { if (t[e] == ',') { end = e; break; } if (e + 3 <= len && !memcmp(t + e, FULLCOMMA, 3)) { end = e; break; } e++; }
            if (end && end > s) { sb_put(&b, t + i, s - i); sb_puts(&b, "<display>"); i = end; continue; }
            sb_put(&b, t + i, s - i); i = s; continue;
        }
        sb_put(&b, t + i, 1); i++;
    }
    *out_len = b.n; return b.p ? b.p : calloc(1, 1);
}
char *dm_sharelog_scrub(const char *text, size_t len, const char *home, const char *appid, const char *const *names, int nnames, size_t *out_len) {
    size_t n = len; char *cur = malloc(len + 1); if (!cur) return NULL; memcpy(cur, text, len); cur[len] = 0; char *nx;
    if (home && strlen(home) > 1) { nx = repl_lit(cur, n, home, "~", &n); free(cur); cur = nx; }
    nx = repl_lit(cur, n, "/storage/emulated/0", "~", &n); free(cur); cur = nx;
    if (appid && *appid) { nx = repl_lit(cur, n, appid, "<app-id>", &n); free(cur); cur = nx; }
    char found[64][64]; int nf = 0; for (int i = 0; i < nnames && nf < 64; i++) if (names[i] && *names[i]) { snprintf(found[nf], sizeof found[nf], "%s", names[i]); nf++; }
    nf = dm_sharelog_names_in_text(cur, n, found, nf, 64);
    for (int i = 0; i < nf; i++) { nx = repl_word(cur, n, found[i], "<character>", &n); free(cur); cur = nx; }
    nx = scan_ids(cur, n, &n); free(cur); cur = nx;
    *out_len = n; return cur;
}
char *dm_sharelog_read_tail(const char *path, size_t limit, size_t *len, unsigned long long *omitted) {
    *len = 0; *omitted = 0; FILE *f = fopen(path, "rb"); if (!f) return NULL;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; } long sz = ftell(f); if (sz < 0) { fclose(f); return NULL; }
    size_t take = (size_t)sz > limit ? limit : (size_t)sz; *omitted = (size_t)sz - take; fseek(f, (long)*omitted, SEEK_SET);
    char *buf = malloc(take + 1); if (!buf) { fclose(f); return NULL; } size_t got = fread(buf, 1, take, f); fclose(f); buf[got] = 0; *len = got;
    if (*omitted) { char *nl = memchr(buf, '\n', got); if (nl) { size_t drop = (size_t)(nl + 1 - buf); memmove(buf, nl + 1, got - drop); got -= drop; buf[got] = 0; *len = got; *omitted += drop; } }   // 从整行开始 | start at a whole line
    return buf;
}
static void last_line(sb_t *h, const char *log, size_t len, const char *key) {
    const char *best = NULL; size_t bl = 0, kl = strlen(key), i = 0;
    while (i < len) { const char *nl = memchr(log + i, '\n', len - i); size_t ll = nl ? (size_t)(nl - (log + i)) : len - i; if (ll >= kl && memmem(log + i, ll, key, kl)) { best = log + i; bl = ll; } i += ll + 1; }
    if (best) { sb_put(h, best, bl); sb_puts(h, "\n"); } else { sb_puts(h, "（没有「"); sb_puts(h, key); sb_puts(h, "」行）\n"); }
}
static void perf_block(sb_t *h, const char *title, const char *log, size_t len) {
    static const char *const keys[] = { "boot: 显示版本", "boot: 导入包", "boot: dm_main", "NVN caps device=", "NVN caps subgroup", "NVN 32-lane warps", "NVN quad shuffles", "NVN native BC", "NVN texture checks", "guest AS", "崩溃围栏：", "[诊断]", "[perf]", "图形设置：", "帧率档：", "自定义设置：读盘", "内存：", "优雅退出", "【红】" };
    sb_puts(h, "\n—— "); sb_puts(h, title); sb_puts(h, " ——\n");
    if (!log) { sb_puts(h, "（没有这份日志）\n"); return; }
    for (size_t i = 0; i < sizeof keys / sizeof *keys; i++) last_line(h, log, len, keys[i]);
}
static int collect_names(const char *files_dir, char names[][64], int cap) {
    static const char *const roots[] = { "save", "save.work", "save.test", "save.test.work" }; int n = 0;
    for (size_t r = 0; r < 4 && n < cap; r++) {
        char d[1100]; snprintf(d, sizeof d, "%s/%s", files_dir, roots[r]); DIR *dir = opendir(d); if (!dir) continue; struct dirent *e;
        while ((e = readdir(dir)) && n < cap) { if (e->d_name[0] == '.' || strlen(e->d_name) >= 64) continue; char p[2400]; snprintf(p, sizeof p, "%s/%s/%s.d2s", d, e->d_name, e->d_name); struct stat st; if (stat(p, &st)) continue;
            int dup = 0; for (int k = 0; k < n; k++) if (!strcmp(names[k], e->d_name)) dup = 1; if (!dup) snprintf(names[n++], 64, "%s", e->d_name); }
        closedir(dir);
    }
    return n;
}
long dm_sharelog_build(const char *runtime_dir, const char *files_dir, const char *out_path, const char *version_label, const char *device_line, const char *appid) {
    char p[1200]; size_t cl = 0, pl = 0; unsigned long long co = 0, po = 0;
    snprintf(p, sizeof p, "%s/boot.log", runtime_dir); char *cur = dm_sharelog_read_tail(p, DM_SHARELOG_TAIL, &cl, &co);
    snprintf(p, sizeof p, "%s/boot.previous.log", runtime_dir); char *prev = dm_sharelog_read_tail(p, DM_SHARELOG_TAIL, &pl, &po);
    sb_t h = {0}; char ts[64]; time_t now = time(NULL); strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S %z", localtime(&now));
    sb_puts(&h, "D2R Mobile Diagnostics (Android)\n导出时间 "); sb_puts(&h, ts); sb_puts(&h, "\n显示版本 "); sb_puts(&h, version_label && *version_label ? version_label : "（无）"); sb_puts(&h, "\n设备 "); sb_puts(&h, device_line ? device_line : "?"); sb_puts(&h, "\n");
    { size_t el = 0; unsigned long long eo = 0; snprintf(p, sizeof p, "%s/exit_reasons.txt", runtime_dir); char *ex = dm_sharelog_read_tail(p, 16384, &el, &eo);
      sb_puts(&h, "\n—— 系统记录的最近退出原因（ApplicationExitInfo）——\n"); if (ex && el) sb_put(&h, ex, el); else sb_puts(&h, "（没有）\n"); free(ex); }
    char line[200]; snprintf(line, sizeof line, "Log export: at most %u bytes per log; omitted prefix bytes current=%llu previous=%llu. Original log files are unchanged.\n", (unsigned)DM_SHARELOG_TAIL, co, po); sb_puts(&h, line);
    perf_block(&h, "本局关键读数（各取最后一条）", cur, cl); perf_block(&h, "上一局关键读数（各取最后一条）", prev, pl);
    sb_puts(&h, "\n==== 本局日志 boot.log ====\n"); if (cur) sb_put(&h, cur, cl); else sb_puts(&h, "（没有）\n");
    sb_puts(&h, "\n==== 上一局日志 boot.previous.log ====\n"); if (prev) sb_put(&h, prev, pl); else sb_puts(&h, "（没有）\n");
    free(cur); free(prev);
    char names[64][64]; int nn = collect_names(files_dir, names, 64); const char *np[64]; for (int i = 0; i < nn; i++) np[i] = names[i];
    char home[1100]; snprintf(home, sizeof home, "%s", files_dir); { char *s = strstr(home, "/files"); if (s) *s = 0; }   // /data/user/0/<包名> | the app's data directory
    size_t fl = 0; char *fin = dm_sharelog_scrub(h.p ? h.p : "", h.n, home, appid, np, nn, &fl); free(h.p); if (!fin) return -1;
    FILE *f = fopen(out_path, "wb"); if (!f) { free(fin); return -1; } size_t w = fwrite(fin, 1, fl, f); int ok = fclose(f) == 0; free(fin);
    return ok && w == fl ? (long)w : -1;
}
