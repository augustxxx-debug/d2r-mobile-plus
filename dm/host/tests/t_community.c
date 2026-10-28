// 社区入口（dm_community.c）：远端清单 community 字段的解析（有 / 无 / 格式坏）、缓存文件的写 / 删 / 回退、按语言组选值（中文群号 / 其他语言链接）、
// 两个入口消息、复制回调计数与日志行、没有剪贴板回调时的失败分支。只用临时目录，不碰真存档；「重启进程」= 把静态状态清回初值再 dm_community_load
// Community entry (dm_community.c): parsing of the remote manifest's community field (present / absent / malformed), cache file write / delete / fallback, value by language group (Chinese = QQ group number / others = link),
// the two entry messages, copy callback count and log lines, the failure branch without a clipboard callback. Temporary directories only, never real saves; "restart" = reset the static state and call dm_community_load again
#include "../dm_custom.c"
#include "../dm_community.c"
#include <unistd.h>
void dm_3d_quality_mode_set(int value) { (void)value; }
static int t_group = DM_LG_ZH;
int dm_lang_group(void) { return t_group; }
const char *dm_save_commitdir(void) { return NULL; }
int dm_3d_quality_mode_get(void) { return 0; }
float dm_3d_scale_cur(void) { return .75f; }
const char *dm_ui(const char *key) { return key; }
const char *dm_crashfence_test_env(void) { return "DM_TEST_X"; }
// 泵用到的游戏侧符号：单测里没有游戏模块（g_nmod = 0 → 泵立刻返回） | game-side symbols the pump references: no game module in the unit test (g_nmod = 0 → the pump returns at once)
module_t g_mod[MAX_MOD]; int g_nmod;
int dm_padmode_current(void) { return 0; }
uint64_t dm_padmode_page_keys(uint64_t m) { (void)m; return 0; }
const char *dm_str(uint8_t *b, const char *k) { (void)b; return k; }
int dm_ui_top_chk_on(void) { return 0; }
void dm_ui_top_chk(uint64_t r, const char *n, uint64_t g) { (void)r; (void)n; (void)g; }
static int checks, fails;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
static char dir[64], cache[128], logp[128];
static int copies; static char copied[256];
static int copy_ok(const char *s) { copies++; snprintf(copied, sizeof copied, "%s", s); return 1; }
static int copy_bad(const char *s) { (void)s; return 0; }
static void restart(void) { g_qq[0] = g_gh[0] = 0; atomic_store(&g_loaded_cm, 0); atomic_store(&g_show, 0); atomic_store(&g_copied, 0); }
static char *log_text(void) { static char t[16384]; fflush(stderr); FILE *f = fopen(logp, "r"); size_t n = f ? fread(t, 1, sizeof t - 1, f) : 0; t[n] = 0; if (f) fclose(f); return t; }
static void log_reset(void) { fflush(stderr); if (ftruncate(fileno(stderr), 0) == 0) rewind(stderr); }   // 截断并回到文件头（只重开文件会从旧偏移接着写、前面留一段空洞） | truncate and rewind (reopening alone keeps writing at the old offset, leaving a hole at the front)
static int cache_has(const char *s) { FILE *f = fopen(cache, "r"); char t[512] = {0}; if (!f) return 0; fread(t, 1, sizeof t - 1, f); fclose(f); return strstr(t, s) != NULL; }
#define M_FULL "{\"build\": 1793043416, \"note\": \"x\", \"urgent\": false, \"community\": { \"qq_group\": \"1129913678\", \"github\": \"https://github.com/example/d2r-mobile-plus/discussions\" } }"
#define M_NONE "{\"build\": 1793043416, \"note\": \"x\", \"urgent\": false}"
#define M_BAD  "{\"build\": 1, \"community\": { \"qq_group\": \"12ab\", \"github\": \"http://evil.example/x y\" } }"
#define M_STR  "{\"build\": 1, \"community\": \"1129913678\" }"
#define M_QQ   "{\"build\": 1, \"community\": { \"qq_group\": \"1129913678\" } }"
#define M_GH   "{\"build\": 1, \"community\": { \"github\": \"https://github.com/example/d2r-mobile-plus\" }, \"note\": \"after\" }"
int main(void) {
    snprintf(dir, sizeof dir, "/tmp/d2r-community-XXXXXX"); if (!mkdtemp(dir)) return 1;
    snprintf(cache, sizeof cache, "%s/community.txt", dir); snprintf(logp, sizeof logp, "%s/log.txt", dir);
    unsetenv("DM_TEST_COMMUNITY"); dm_custom_init(dir);
    if (!freopen(logp, "w", stderr)) return 1;
    char v[256]; int k;
    // 1. 没有缓存：入口不显示 | 1. No cache: the entry is hidden
    restart(); dm_community_load(); CHECK(!dm_community_available()); CHECK(dm_community_value(DM_LG_ZH, v, sizeof v) == 0); CHECK(dm_community_value(DM_LG_EN, v, sizeof v) == 0);
    // 2. 清单有字段：两个值都进内存与缓存；中文取群号、其他语言取链接 | 2. Manifest has the field: both values in memory and cache; Chinese picks the group number, other languages the link
    log_reset(); CHECK(dm_community_manifest(M_FULL, strlen(M_FULL)) == 1);
    k = dm_community_value(DM_LG_ZH, v, sizeof v); CHECK(k == 1); CHECK(!strcmp(v, "1129913678"));
    k = dm_community_value(DM_LG_EN, v, sizeof v); CHECK(k == 2); CHECK(!strcmp(v, "https://github.com/example/d2r-mobile-plus/discussions"));
    k = dm_community_value(DM_LG_RU, v, sizeof v); CHECK(k == 2); k = dm_community_value(DM_LG_KO, v, sizeof v); CHECK(k == 2); k = dm_community_value(DM_LG_MX, v, sizeof v); CHECK(k == 2);
    CHECK(dm_community_available()); CHECK(cache_has("qq_group=1129913678\n")); CHECK(cache_has("github=https://github.com/example/d2r-mobile-plus/discussions\n"));
    CHECK(strstr(log_text(), "社区入口：清单有字段")); CHECK(strstr(log_text(), "群号=1129913678"));
    // 3. 重启：从缓存读回 | 3. Restart: read back from the cache
    restart(); dm_community_load(); CHECK(dm_community_available()); k = dm_community_value(DM_LG_ZH, v, sizeof v); CHECK(k == 1 && !strcmp(v, "1129913678"));
    // 4. 这次没拉到：沿用缓存（内存值不动、文件不动） | 4. Fetch failed this time: keep the cache (memory and file untouched)
    log_reset(); CHECK(dm_community_manifest(NULL, 0) == -1); CHECK(dm_community_available()); CHECK(cache_has("qq_group=")); CHECK(strstr(log_text(), "社区入口：清单拉取失败，沿用缓存"));
    // 5. 清单不是合法 JSON（截断）：十一审 Q11-01 起当格式错 = 无字段（入口不显示、缓存清掉），记「社区入口：清单格式错」 | 5. Manifest is not valid JSON (truncated): since Q11-01 a format error = no field (entry hidden, cache cleared), logged as "manifest malformed"
    log_reset(); CHECK(dm_community_manifest("{\"build\": 1, \"community\": { \"qq_group\": \"11", 42) == 0); CHECK(!dm_community_available()); CHECK(access(cache, F_OK) != 0); CHECK(strstr(log_text(), "社区入口：清单格式错"));
    CHECK(dm_community_manifest(M_FULL, strlen(M_FULL)) == 1);
    // 6. 清单取到了但没有字段：入口不显示、缓存删掉；重启后仍不显示 | 6. Fetched without the field: entry hidden, cache deleted; still hidden after a restart
    log_reset(); CHECK(dm_community_manifest(M_NONE, strlen(M_NONE)) == 0); CHECK(!dm_community_available()); CHECK(access(cache, F_OK) != 0); CHECK(strstr(log_text(), "社区入口：清单无字段"));
    restart(); dm_community_load(); CHECK(!dm_community_available());
    // 7. 字段格式坏（群号带字母、链接不是 github 的 https 地址 / 带空格）：当没有字段 | 7. Malformed field (letters in the number, link not an https github address / with a space): treated as absent
    CHECK(dm_community_manifest(M_FULL, strlen(M_FULL)) == 1);
    log_reset(); CHECK(dm_community_manifest(M_BAD, strlen(M_BAD)) == 0); CHECK(!dm_community_available()); CHECK(access(cache, F_OK) != 0); CHECK(strstr(log_text(), "【红】社区入口"));
    CHECK(dm_community_manifest(M_FULL, strlen(M_FULL)) == 1); CHECK(dm_community_manifest(M_STR, strlen(M_STR)) == 0); CHECK(!dm_community_available());
    // 8. 只有群号：中文显示，其他语言不显示；只有链接：全部语言显示链接 | 8. Number only: shown in Chinese, hidden elsewhere; link only: every language shows the link
    CHECK(dm_community_manifest(M_QQ, strlen(M_QQ)) == 1); CHECK(dm_community_value(DM_LG_ZH, v, sizeof v) == 1); CHECK(dm_community_value(DM_LG_EN, v, sizeof v) == 0);
    t_group = DM_LG_ZH; CHECK(dm_community_available()); t_group = DM_LG_EN; CHECK(!dm_community_available()); CHECK(cache_has("qq_group=1129913678\n")); CHECK(!cache_has("github="));
    CHECK(dm_community_manifest(M_GH, strlen(M_GH)) == 1); CHECK(dm_community_value(DM_LG_ZH, v, sizeof v) == 2); CHECK(dm_community_value(DM_LG_EN, v, sizeof v) == 2); CHECK(!cache_has("qq_group="));
    t_group = DM_LG_ZH; CHECK(dm_community_available());
    // 9. 入口消息：有值时挂上提示框；没值时不挂 | 9. Entry messages: queue the box when there is a value; nothing without one
    CHECK(dm_community_manifest(M_FULL, strlen(M_FULL)) == 1);
    dm_community_on_uimsg("PanelManager", "ClosePanel", "TouchCommunity"); CHECK(atomic_load(&g_show) == 1); atomic_store(&g_show, 0);
    dm_community_on_uimsg("PanelManager", "ClosePanel", "TouchCommunityOpt"); CHECK(atomic_load(&g_show) == 1); atomic_store(&g_show, 0);
    dm_community_on_uimsg("PanelManager", "OpenPanel", "TouchCommunity"); CHECK(atomic_load(&g_show) == 0);
    CHECK(dm_community_manifest(M_NONE, strlen(M_NONE)) == 0); dm_community_on_uimsg("PanelManager", "ClosePanel", "TouchCommunity"); CHECK(atomic_load(&g_show) == 0);
    // 10. 复制：显示着时「复制」消息 → 回调一次、拿到的是当前语言的值、记日志；没显示时不认 | 10. Copy: the copy message while shown → one callback with the current language's value, logged; ignored when not shown
    CHECK(dm_community_manifest(M_FULL, strlen(M_FULL)) == 1); dm_community_copy_init(copy_ok);
    log_reset(); atomic_store(&g_show, 2); t_group = DM_LG_ZH; dm_community_on_uimsg("PanelManager", "ClosePanel", "TouchCommCopy");
    CHECK(copies == 1); CHECK(dm_community_copy_count() == 1); CHECK(!strcmp(copied, "1129913678")); CHECK(atomic_load(&g_copied) == 1); CHECK(strstr(log_text(), "社区入口：复制 群号"));
    log_reset(); t_group = DM_LG_EN; dm_community_on_uimsg("PanelManager", "ClosePanel", "TouchCommCopy");
    CHECK(copies == 2); CHECK(!strcmp(copied, "https://github.com/example/d2r-mobile-plus/discussions")); CHECK(strstr(log_text(), "社区入口：复制 链接"));
    atomic_store(&g_show, 0); dm_community_on_uimsg("PanelManager", "ClosePanel", "TouchCommCopy"); CHECK(copies == 2);
    // 11. 剪贴板回调失败 / 没登记：记失败、不计数 | 11. Clipboard callback fails / not registered: failure recorded, not counted
    dm_community_copy_init(copy_bad); atomic_store(&g_show, 2); log_reset(); dm_community_on_uimsg("PanelManager", "ClosePanel", "TouchCommCopy"); CHECK(atomic_load(&g_copied) == -1); CHECK(dm_community_copy_count() == 2); CHECK(strstr(log_text(), "【红】社区入口：复制失败"));
    dm_community_copy_init(NULL); atomic_store(&g_copied, 0); dm_community_on_uimsg("PanelManager", "ClosePanel", "TouchCommCopy"); CHECK(atomic_load(&g_copied) == -1);
    // 12. 「确定」消息：收起 | 12. The OK message: dismiss
    dm_community_on_uimsg("PanelManager", "ClosePanel", "TouchCommOk"); CHECK(atomic_load(&g_ack) == 1); atomic_store(&g_ack, 0); atomic_store(&g_show, 0);
    // 13. 测试开关 DM_TEST_COMMUNITY=<清单文件>：测试局从文件读清单（产品局不读） | 13. Test switch DM_TEST_COMMUNITY=<manifest file>: a test run reads the manifest from the file
    { char mp[160]; snprintf(mp, sizeof mp, "%s/sample.json", dir); FILE *f = fopen(mp, "w"); fputs(M_QQ, f); fclose(f);
      CHECK(dm_community_manifest(M_NONE, strlen(M_NONE)) == 0); t_group = DM_LG_ZH; setenv("DM_TEST_COMMUNITY", mp, 1); restart(); dm_community_load(); CHECK(dm_community_available()); unsetenv("DM_TEST_COMMUNITY"); }
    // 14. 超长 / 越界：群号 13 位、链接 300 字符都当格式坏；缓冲不越界 | 14. Too long: a 13-digit number and a 300-character link are malformed; no buffer overrun
    { char big[512]; snprintf(big, sizeof big, "{\"community\": { \"qq_group\": \"1234567890123\", \"github\": \"https://github.com/%0280d\" } }", 0); CHECK(dm_community_manifest(big, strlen(big)) == 0); CHECK(!dm_community_available()); }
    // 16. 十一审 Q11-01：严格 JSON 与层级——语法错 / 尾逗号 / 非对象根 / 重复字段 / 嵌套同名键 / 字符串转义 都不算有字段；深层同名键被忽略、无关字段不碍事
    // 16. Q11-01: strict JSON and nesting - syntax error / trailing comma / non-object root / duplicate field / nested same-name key / string escapes never count as the field; deeper same-name keys are ignored, unrelated fields are fine
    t_group = DM_LG_ZH;
    { static const char *const bad[] = {
        "{\"build\": 1 \"community\": { \"qq_group\": \"123456\" } }",                         // 缺逗号 | missing comma
        "{\"build\": 1, \"community\": { \"qq_group\": \"123456\", }, }",                      // 尾逗号 | trailing commas
        "[{\"community\": {\"qq_group\": \"123456\"}}]",                                      // 非对象根 | non-object root
        "{\"community\": {\"qq_group\": \"123456\"}} {}",                                     // 根后面还有东西 | trailing garbage
        "{\"build\":1,\"meta\":{\"community\":{\"qq_group\":\"123456\"}}}",                     // 嵌套 community | nested community
        "{\"community\": {\"qq_group\": \"123456\"}, \"community\": {\"qq_group\": \"654321\"}}",   // 重复 community | duplicate community
        "{\"community\": {\"qq_group\": \"123456\", \"qq_group\": \"654321\"}}",               // 重复 qq_group | duplicate qq_group
        "{\"community\": {\"qq_group\": \"1234\\u0035678\"}}",                                // 转义 | escape
        "{\"community\": {\"github\": \"https://github.com/a\\/b\"}}",                       // 转义的斜杠 | escaped slash
        "{\"community\": [\"qq_group\", \"123456\"]}",                                        // community 不是对象 | community not an object
        "{\"community\": {\"qq_group\": 123456}}",                                              // 值不是字符串 | value not a string
        "{\"community\": {\"qq_group\": \"123456\"}, \"build\": 01}",                           // 数字语法错 | bad number
        "{\"community\": {\"qq_group\": \"123456\"}, \"note\": \"no end}",                       // 字符串没收尾 | unterminated string
        "{\"community\": {\"qq_group\": \"123456\"}, \"x\": tru}" };                              // 字面量错 | bad literal
      for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) { CHECK(dm_community_manifest(M_FULL, strlen(M_FULL)) == 1); log_reset();
          int r = dm_community_manifest(bad[i], strlen(bad[i])); if (r != 0 || dm_community_available() || access(cache, F_OK) == 0 || !strstr(log_text(), "社区入口：清单")) { fails++; checks++; printf("FAIL Q11-01 坏清单 %zu: 返回 %d 可用 %d 缓存在 %d: %s\n", i, r, dm_community_available(), access(cache, F_OK) == 0, bad[i]); } else checks++; } }
    { static const char *const good[] = {
        "{\"community\":{\"x\":{\"qq_group\":\"999999\"},\"qq_group\":\"123456\"}}",             // 深层同名键被忽略 | deeper same-name key ignored
        "{\"community\":{\"github\":\"https://github.com/x/y\",\"extra\":[1,-2.5e3,true,null,{\"a\":\"b\\n\"}],\"qq_group\":\"123456\"},\"build\":7,\"note\":\"\\\"引号\\\"\"}",   // 无关字段任意 | unrelated fields anything
        " \n{ \"build\" : 1 , \"community\" : { \"qq_group\" : \"123456\" } } \n" };               // 空白 | whitespace
      for (size_t i = 0; i < sizeof good / sizeof *good; i++) { CHECK(dm_community_manifest(M_NONE, strlen(M_NONE)) == 0);
          int r = dm_community_manifest(good[i], strlen(good[i])); k = dm_community_value(DM_LG_ZH, v, sizeof v);
          if (r != 1 || k != 1 || strcmp(v, "123456")) { fails++; checks++; printf("FAIL Q11-01 好清单 %zu: 返回 %d 种类 %d 值 %s\n", i, r, k, v); } else checks++; } }
    // 17. 鸣谢（用户 2026-10-28）：名单拼行——每行 ≤ 4 名、估算宽 ≤ 44 单位、特别鸣谢末尾接括号说明、鸣谢为空不出行、封顶 6 行加「…」、中文「、」其他「, 」
    // 17. Credits: line assembly - ≤ 4 names per line, estimated width ≤ 44 units, the special list ends with the note, an empty thanks list shows no line, capped at 6 lines with "…", "、" in Chinese and ", " elsewhere
    { cr_line_t ln[CR_LINES]; int tr;
      int n = credits_lines(k_special_default, 1, NULL, 0, "、", "（d2r-mobile 原作者）", ln, CR_LINES, &tr); CHECK(n == 1); CHECK(ln[0].kind == 1); CHECK(!strcmp(ln[0].text, "jhaizhou-ops（d2r-mobile 原作者）")); CHECK(!tr);
      n = credits_lines(k_special_default, 1, NULL, 0, ", ", " (author of d2r-mobile)", ln, CR_LINES, &tr); CHECK(n == 1); CHECK(!strcmp(ln[0].text, "jhaizhou-ops (author of d2r-mobile)"));
      static const char *const nine[] = { "a1", "b2", "c3", "d4", "e5", "f6", "g7", "h8", "i9" }; static const char *const two[] = { "x", "y" };
      n = credits_lines(nine, 9, two, 2, ", ", "", ln, CR_LINES, &tr); CHECK(n == 4); CHECK(ln[0].kind == 1 && ln[1].kind == 0 && ln[2].kind == 0 && ln[3].kind == 2);
      CHECK(!strcmp(ln[0].text, "a1, b2, c3, d4")); CHECK(!strcmp(ln[2].text, "i9")); CHECK(!strcmp(ln[3].text, "x, y")); CHECK(!tr);
      n = credits_lines(nine, 9, two, 2, "、", "", ln, CR_LINES, &tr); CHECK(!strcmp(ln[0].text, "a1、b2、c3、d4"));
      static const char *const longn[] = { "abcdefghijklmnopqrst", "abcdefghijklmnopqrsu", "abcdefghijklmnopqrsv" };   // 20 字节 × 3：按宽折成两行 | 20 bytes × 3: wrapped by width into two lines
      n = credits_lines(longn, 3, NULL, 0, ", ", "", ln, CR_LINES, &tr); CHECK(n == 2); CHECK(cr_units(ln[0].text) <= CR_LINE_UNITS); CHECK(ln[1].kind == 0);
      static const char *const many[20] = { "n01", "n02", "n03", "n04", "n05", "n06", "n07", "n08", "n09", "n10", "n11", "n12", "n13", "n14", "n15", "n16", "n17", "n18", "n19", "n20" };
      n = credits_lines(many, 20, many, 20, ", ", "", ln, CR_LINES, &tr); CHECK(n == CR_LINES); CHECK(tr); CHECK(strstr(ln[CR_LINES - 1].text, "…"));
      n = credits_lines(NULL, 0, NULL, 0, ", ", "", ln, CR_LINES, &tr); CHECK(n == 0); CHECK(!tr); }
    // 18. 清单 credits 字段（RC2 用，RC1 只解析计数）：数组只收 ≤ 32 字节、不带转义的字符串，去重，合计 ≤ 24；坏形状整个忽略并记红，社区字段照常
    // 18. Manifest credits field (for RC2; RC1 only parses and counts): arrays take only escape-free strings ≤ 32 bytes, deduped, ≤ 24 in all; a bad shape is ignored as a whole with a red line; the community field is unaffected
    { static const char *const c1 = "{\"community\": {\"qq_group\": \"123456\"}, \"credits\": {\"special\": [\"a\", \"b\", \"a\"], \"thanks\": [\"c\"]}}";
      log_reset(); CHECK(dm_community_manifest(c1, strlen(c1)) == 1); CHECK(strstr(log_text(), "credits=特别 2 / 鸣谢 1")); CHECK(!strstr(log_text(), "【红】"));
      static const char *const c2 = "{\"community\": {\"qq_group\": \"123456\"}, \"credits\": \"jhaizhou-ops\"}";
      log_reset(); CHECK(dm_community_manifest(c2, strlen(c2)) == 1); CHECK(strstr(log_text(), "【红】社区入口：清单 credits 字段格式坏")); CHECK(strstr(log_text(), "credits=特别 0 / 鸣谢 0")); CHECK(dm_community_available());
      static const char *const c3 = "{\"community\": {\"qq_group\": \"123456\"}, \"credits\": {\"special\": [1, \"ok\"]}}";
      log_reset(); CHECK(dm_community_manifest(c3, strlen(c3)) == 1); CHECK(strstr(log_text(), "credits 字段格式坏"));
      static const char *const c4 = "{\"community\": {\"qq_group\": \"123456\"}, \"credits\": {\"special\": [\"abcdefghijklmnopqrstuvwxyz0123456789\"]}}";   // 36 字节 | 36 bytes
      log_reset(); CHECK(dm_community_manifest(c4, strlen(c4)) == 1); CHECK(strstr(log_text(), "credits 字段格式坏"));
      static const char *const c5 = "{\"credits\": {\"special\": [\"a\"]}}";   // 只有 credits、没有 community：仍是无字段 | credits only: still no community field
      CHECK(dm_community_manifest(c5, strlen(c5)) == 0); CHECK(!dm_community_available()); }
    // 15. 没有 UI（单测没有游戏模块）：每帧泵不崩 | 15. No UI (no game module in the unit test): the per-frame pump does not crash
    atomic_store(&g_show, 1); dm_community_pump(); dm_community_pump(); atomic_store(&g_show, 0);
    unlink(cache); unlink(logp); { char mp[160]; snprintf(mp, sizeof mp, "%s/sample.json", dir); unlink(mp); } rmdir(dir);
    fflush(stderr); printf("t_community: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
