// 百宝囊悬浮清单里的一行物品名：把 annot_drop 给高级符文加的颜色码与上下框线去掉，只留带 #号 的那一行。
// 开 item_notes 时 r24 起的符文名是「ÿc8————————\n\nÿc8贝 ÿc5Berÿc2#30\n\nÿc8————————」这种多行带色串，
// 百宝囊的普通文字框会把颜色码画成 *8、换行画成空格或另一行框线；这里只取名字那一行。
// Clean one item name for the pouch tooltip: drop the colour codes and the frame lines annot_drop adds to high
// runes, keeping only the line that carries #N. With item_notes on, rune names from #24 are multi-line coloured
// strings like 「ÿc8————\n\nÿc8贝 ÿc5Berÿc2#30\n\nÿc8————」; the pouch's plain text box would draw the colour
// codes as *8 and the newlines as spaces or extra frame lines, so keep only the name line.
#ifndef DM_POUCH_NAME_H
#define DM_POUCH_NAME_H
#include <stddef.h>
#include <string.h>

// 空白（含行尾 \r）| Whitespace (including a trailing \r)
static inline int dm_pouch_name_ws(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f' || c == '\n';
}

// 整行只有框线字符：- = _ 空格 / 制表，以及 —（U+2014，annot_drop 实际用的）与 ─（U+2500，同义框线）
// The whole line is frame characters only: - = _ space / tab, plus — (U+2014, what annot_drop really uses) and ─ (U+2500)
static inline int dm_pouch_name_frame_line(const char *s) {
    for (size_t i = 0; s[i]; ) {
        unsigned char c = (unsigned char)s[i];
        if (c == '-' || c == '=' || c == '_' || c == ' ' || c == '\t' || c == '\v' || c == '\f') { i++; continue; }
        if (c == 0xE2 && (unsigned char)s[i + 1] == 0x94 && (unsigned char)s[i + 2] == 0x80) { i += 3; continue; }   // ─
        if (c == 0xE2 && (unsigned char)s[i + 1] == 0x80 && (unsigned char)s[i + 2] == 0x94) { i += 3; continue; }   // —
        return 0;
    }
    return 1;
}

// in -> out（至多 n-1 字节 + 结尾 0）；纯函数，不改 in。 | in -> out (at most n-1 bytes plus the terminator); pure, does not modify in.
// 颜色码 = ÿc + 1 字符（ÿ 为 UTF-8 的 0xC3 0xBF，也认单字节 0xFF）| Colour code = ÿc + 1 character (ÿ is UTF-8 0xC3 0xBF, or the single byte 0xFF)
static inline void dm_pouch_clean_name(const char *in, char *out, size_t n) {
    char line[512], best[512]; size_t bl = 0, ll = 0; int have = 0;
    if (!out || !n) return;
    out[0] = 0;
    if (!in) return;
    for (size_t i = 0; ; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c != 0 && c != '\n') {
            if (c == 0xC3 && (unsigned char)in[i + 1] == 0xBF && in[i + 2] == 'c' && in[i + 3]) { i += 3; continue; }
            if (c == 0xFF && in[i + 1] == 'c' && in[i + 2]) { i += 2; continue; }
            if (c != '\r' && ll + 1 < sizeof line) line[ll++] = (char)c;
            continue;
        }
        line[ll] = 0;
        {   size_t a = 0, b = ll;
            while (a < b && dm_pouch_name_ws((unsigned char)line[a])) a++;
            while (b > a && dm_pouch_name_ws((unsigned char)line[b - 1])) b--;
            if (b > a) {
                int hash = 0;
                for (size_t j = a; j + 1 < b; j++) if (line[j] == '#' && line[j + 1] >= '0' && line[j + 1] <= '9') { hash = 1; break; }
                if (hash || !dm_pouch_name_frame_line(line + a)) {   // 带 #号 的行优先；否则记住最后一个非空非框线行 | a line with #N wins; otherwise remember the last non-empty non-frame line
                    size_t len = b - a; if (len > sizeof best - 1) len = sizeof best - 1;
                    memcpy(best, line + a, len); best[len] = 0; bl = len; have = 1;
                    if (hash) break;
                }
            }
        }
        ll = 0;
        if (c == 0) break;
    }
    if (have) { size_t k = bl < n - 1 ? bl : n - 1; memcpy(out, best, k); out[k] = 0; }
}
#endif
