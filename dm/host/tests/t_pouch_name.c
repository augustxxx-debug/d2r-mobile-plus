// 百宝囊清单物品名清洗单测（秒级）：dm_pouch_name.h 的纯函数，不碰游戏。
// 低号符文 / 英文符文 / 宝石单行时原样；高号符文名（item_notes 开）带颜色码、上下框线与空行，只取含 #号 的那一行；
// 没有 #号 的多行取最后一个非空非框线行；空串给空串；超长输入按输出缓冲截断、不越界。
// Unit test of the pouch list's item-name cleaner (takes seconds): the pure function in dm_pouch_name.h, without touching
// the game. Low runes / English runes / gems on one line pass through unchanged; high-rune names (with item_notes on)
// carry colour codes, top / bottom frame lines and blank lines, and only the line with #N is kept; a multi-line name
// without #N takes the last non-empty non-frame line; an empty input gives an empty output; over-long input is
// truncated to the output buffer without writing out of bounds.
#include <stdio.h>
#include <string.h>
#include "../dm_pouch_name.h"
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  ✗ "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static void check(const char *in, const char *want, const char *what) {
    char out[256]; dm_pouch_clean_name(in, out, sizeof out);
    CHECK(!strcmp(out, want), "%s：得「%s」，期望「%s」", what, out, want);
}
int main(void) {
    check("拉尔 Ral #8", "拉尔 Ral #8", "低号符文单行原样");
    check("\xC3\xBF" "c8拉尔 \xC3\xBF" "c5Ral\xC3\xBF" "c2#8", "拉尔 Ral#8", "低号符文去颜色码");
    check("\xC3\xBF" "c8—————————————\n\n\n\n"
          "\xC3\xBF" "c8贝 \xC3\xBF" "c5Ber\xC3\xBF" "c2#30\n\n\n\n"
          "\xC3\xBF" "c8—————————————", "贝 Ber#30", "zhCN 高号符文取 #号 行");
    check("\xC3\xBF" "c8—————————————\n\n\n\n"
          "\xC3\xBF" "c8Runa \xC3\xBF" "c5Ber\xC3\xBF" "c2#30\n\n\n\n"
          "\xC3\xBF" "c8—————————————", "Runa Ber#30", "esES 高号符文取 #号 行");
    check("Ral Rune #8", "Ral Rune #8", "英文符文单行原样");
    check("完美的红宝石", "完美的红宝石", "宝石名单行不变");
    check("", "", "空串给空串");
    check("\xC3\xBF" "c8Runa El\xC3\xBF" "c2#1", "Runa El#1", "低号符文颜色码在名字中间");
    check("\xE2\x80\x94\n\nfoo bar\n\n\xE2\x80\x94", "foo bar", "无 #号 取最后一个非框线行");
    check("\xE2\x94\x80" "\xE2\x94\x80" "\n名字\n" "\xE2\x94\x80" "\xE2\x94\x80", "名字", "U+2500 框线行跳过");
    check("\xFF" "c1Hello", "Hello", "单字节 0xFF 颜色码");
    {   char big[1200]; struct { char out[8]; char canary[8]; } s;
        memset(big, 'A', sizeof big - 1); big[sizeof big - 1] = 0;
        memset(&s, 0x7f, sizeof s);
        dm_pouch_clean_name(big, s.out, sizeof s.out);
        CHECK(strlen(s.out) == 7 && !memcmp(s.out, "AAAAAAA", 7), "超长输入截到 7 字节（得 %u）", (unsigned)strlen(s.out));
        int ok = 1; for (size_t i = 0; i < sizeof s.canary; i++) if (s.canary[i] != 0x7f) ok = 0;
        CHECK(ok, "超长输入没写越界"); }
    printf("t_pouch_name: %d 项，%d 失败\n", checks, fails); return fails != 0;
}
