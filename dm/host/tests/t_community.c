// 社区入口（dm_community.c）：远端清单 community 字段的解析（有 / 无 / 格式坏）、缓存文件的写 / 删 / 回退、按语言组选值（中文群号 / 其他语言链接）、
// 两个入口消息、复制回调计数与日志行、没有剪贴板回调时的失败分支。只用临时目录，不碰真存档；「重启进程」= 把静态状态清回初值再 dm_community_load
// Community entry (dm_community.c): parsing of the remote manifest's community field (present / absent / malformed), cache file write / delete / fallback, value by language group (Chinese = QQ group number / others = link),
// the two entry messages, copy callback count and log lines, the failure branch without a clipboard callback. Temporary directories only, never real saves; "restart" = reset the static state and call dm_community_load again
#include "../dm_custom.c"
#include "../dm_community.c"
#include "dm_touch_controls.h"
#include <unistd.h>
void dm_3d_quality_mode_set(int value) { (void)value; }
static int t_group = DM_LG_ZH;
int dm_lang_group(void) { return t_group; }
const char *dm_save_commitdir(void) { return NULL; }
int dm_3d_quality_mode_get(void) { return 0; }
float dm_3d_scale_cur(void) { return .75f; }
const char *dm_ui(const char *key) { return key; }
const char *dm_crashfence_test_env(void) { return "DM_TEST_X"; }
const char *dm_update_label(void) { return "1.0.0+1 · test"; }
// 泵用到的游戏侧符号：单测里没有游戏模块（g_nmod = 0 → 泵立刻返回） | game-side symbols the pump references: no game module in the unit test (g_nmod = 0 → the pump returns at once)
module_t g_mod[MAX_MOD]; int g_nmod;
int dm_padmode_current(void) { return 0; }
uint64_t dm_padmode_page_keys(uint64_t m) { (void)m; return 0; }
const char *dm_str(uint8_t *b, const char *k) { (void)b; return k; }
int dm_ui_top_chk_on(void) { return 0; }
// RC1 合并后 dm_custom.c 引用的界面 / 控件接口（手柄 HUD 分支的选中框与滚动），本测试不走那些路径，给空桩 | Stubs for the UI / widget calls dm_custom.c gained from the controller-HUD branch; this test never reaches them
uint64_t dm_guest_frame(void) { return 0; }
uint32_t dm_ui_gen(void) { return 1; }
int dm_ui_modal_active(uint64_t root) { (void)root; return 0; }
void dm_ui_wheel_on(uint64_t w, int notches) { (void)w; (void)notches; }
uint64_t dm_wref_get(dm_wref *r) { return r ? r->w : 0; }
void dm_ui_top_chk(uint64_t r, const char *n, uint64_t g) { (void)r; (void)n; (void)g; }
static int checks, fails;
static int t_rect_hit(uint64_t w, uint64_t xy) { int32_t *r = (int32_t *)(uintptr_t)(w + 0x70); int x = (int32_t)(uint32_t)xy, y = (int32_t)(xy >> 32); return x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3]; }   // 假控件的命中判断（虚表 +0x48） | fake widgets' hit test (vtable +0x48)
static int chs(int lg, char *first) { int c[2]; int n = dm_community_channels(lg, c); first[0] = n ? (char)c[0] : 0; return n; }   // 渠道行数，first[0] = 第一行渠道 | channel rows; first[0] = the first row's channel
static int t_recalcs; static void t_recalc(uint64_t w) { (void)w; t_recalcs++; }   // 假控件虚表 +0x10（重算）：只计数 | fake widget vtable +0x10 (recompute): counts only
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
    dm_community_on_uimsg("PanelManager", "ClosePanel", "TouchCommunityFe"); CHECK(atomic_load(&g_show) == 1); CHECK(atomic_load(&g_mode) == 1); atomic_store(&g_show, 0);   // 主菜单「加入社区讨论」 | main-menu Join the community
    log_reset(); dm_community_on_uimsg("PanelManager", "ClosePanel", "TouchAbout"); CHECK(atomic_load(&g_show) == 1); CHECK(atomic_load(&g_mode) == 2); CHECK(strstr(log_text(), "社区入口：点了「关于」")); atomic_store(&g_show, 0);   // 主菜单「关于」：只有版本 + 鸣谢的框 | main-menu About: the version + credits box
    dm_community_on_uimsg("PanelManager", "OpenPanel", "TouchCommunity"); CHECK(atomic_load(&g_show) == 0);
    CHECK(dm_community_manifest(M_NONE, strlen(M_NONE)) == 0); dm_community_on_uimsg("PanelManager", "ClosePanel", "TouchCommunity"); CHECK(atomic_load(&g_show) == 0);
    dm_community_on_uimsg("PanelManager", "ClosePanel", "TouchCommunityFe"); CHECK(atomic_load(&g_show) == 0);   // 没有社区值：社区框不开 | no community value: the community box does not open
    dm_community_on_uimsg("PanelManager", "ClosePanel", "TouchAbout"); CHECK(atomic_load(&g_show) == 1); CHECK(atomic_load(&g_mode) == 2); atomic_store(&g_show, 0);   // 「关于」框照开 | the About box still opens
    // 10. 复制：显示着时「复制」消息 → 回调一次、拿到的是当前语言的值、记日志；没显示时不认 | 10. Copy: the copy message while shown → one callback with the current language's value, logged; ignored when not shown
    CHECK(dm_community_manifest(M_FULL, strlen(M_FULL)) == 1); dm_community_copy_init(copy_ok);
    log_reset(); atomic_store(&g_show, 2); t_group = DM_LG_ZH; dm_community_on_uimsg("PanelManager", "ClosePanel", "TouchCommCopy");
    CHECK(copies == 1); CHECK(dm_community_copy_count() == 1); CHECK(!strcmp(copied, "1129913678")); CHECK(atomic_load(&g_copied) == 1); CHECK(strstr(log_text(), "社区入口：复制 群号"));
    log_reset(); t_group = DM_LG_EN; dm_community_on_uimsg("PanelManager", "ClosePanel", "TouchCommCopy");
    CHECK(copies == 2); CHECK(!strcmp(copied, "https://github.com/example/d2r-mobile-plus/discussions")); CHECK(strstr(log_text(), "社区入口：复制 GitHub"));
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
    // 19. 十二审 Q12-01：「关于」组的社区行跟别的行一样，标签运行时右移 DM_ROW_INDENT、手柄选中框左缘 = 标签 − 30 = +10，不压字；分节标题（带分隔线）不缩进。
    //     按布局搭一棵假控件树（名字 +8、可见 +0x50、子控件 +0x58 / +0x60、矩形 +0x70、虚表 +0 的 +0x10 = 重算），跑真的 indent_rows（dm_custom.c）。RC1.1 B008 起组里没有鸣谢子块
    // 19. Twelfth review Q12-01: the About group's community row behaves like the other rows -- its label moves right by DM_ROW_INDENT at run time and the controller frame's left edge = label - 30 = +10, clear of the text;
    //     the section title (with its divider) is not indented. A fake widget tree mirroring the layout runs the real indent_rows (dm_custom.c). From RC1.1 B008 the group has no credits sub-block
    { enum { NW = 16 }; static uint8_t wb[NW][0x200]; static uint64_t kids[NW][8]; static void (*vt[4])(uint64_t); vt[2] = t_recalc; int nw = 0;
      #define MKW(var, name, x, y) uint64_t var = (uint64_t)(uintptr_t)wb[nw]; *(uint64_t *)(var) = (uint64_t)(uintptr_t)vt; *(const char **)(var + 8) = name; wb[nw][0x50] = wb[nw][0x51] = 1; \
          ((int32_t *)(var + 0x70))[0] = x; ((int32_t *)(var + 0x70))[1] = y; ((int32_t *)(var + 0x70))[2] = 1000; ((int32_t *)(var + 0x70))[3] = 180; nw++
      #define KIDS(parent, ...) do { uint64_t ks_[] = { __VA_ARGS__ }; int i_ = (int)(((uint8_t *)(uintptr_t)(parent) - wb[0]) / 0x200); memcpy(kids[i_], ks_, sizeof ks_); \
          *(uint64_t *)((parent) + 0x58) = (uint64_t)(uintptr_t)kids[i_]; *(uint64_t *)((parent) + 0x60) = sizeof ks_ / sizeof *ks_; } while (0)
      MKW(rows, "TouchCustomRows", 0, 0); MKW(toggle_text, "TouchCustom_x_Text", 0, 0); MKW(about, "TouchAboutRow", 0, 1800); MKW(title, "TouchAboutTitle", 0, 0); MKW(div, "TouchAboutDivider", 0, 90);
      MKW(comm, "TouchAboutCommunityRow", 0, 180); MKW(ctext, "TouchAboutCommunityText", 0, 0); MKW(opt, "TouchCommunityOpt", 1110, 30);
      KIDS(rows, toggle_text, about); KIDS(about, title, comm); KIDS(title, div); KIDS(comm, ctext, opt);
      #undef MKW
      #undef KIDS
      t_group = DM_LG_ZH;
      for (int pass = 0; pass < 3; pass++) { if (pass == 2) t_recalcs = 0; indent_rows(rows, 0); }   // 每帧都跑：第三帧不再有任何改动 | runs every frame: by the third frame nothing changes
      int32_t *tr = (int32_t *)(ctext + 0x70);
      CHECK(tr[0] == DM_ROW_INDENT);                                    // 社区行标签 40（同其他行） | the community label at 40 like the other rows
      CHECK(((int32_t *)(toggle_text + 0x70))[0] == DM_ROW_INDENT);
      CHECK(((int32_t *)(opt + 0x70))[0] == 1110);                       // 按钮不动 | the button stays
      CHECK(dm_sel_frame_x(tr[0]) == DM_SEL_FRAME_MIN_X && dm_sel_frame_x(tr[0]) == 10);   // 选中框左缘 +10（dm_custom.c 用 dm_sel_frame_x(标签 x)） | frame left edge +10
      CHECK(dm_sel_frame_x(tr[0]) + DM_SEL_FRAME_PAD <= tr[0]);          // 框线在字左边 30，不压字 | the frame line is 30 left of the text, clear of it
      CHECK(((int32_t *)(title + 0x70))[0] == 0);                        // 分节标题（带分隔线）不缩进，同原版分节 | the section title (with divider) is not indented, like the original sections
      CHECK(t_recalcs == 0);                                             // 稳定：不来回拉扯 | stable: no tug-of-war
    }
    // 20. 署名（用户 2026-10-29）：「制作：Augustus」排在「特别鸣谢」前，同一套拼行规则（kind 3，每行 ≤ 4 名、≤ 44 单位）；名字代码常量、不翻译；空名单不出行；提示框只有两行时超出加「…」
    // 20. Maker credit (user, 2026-10-29): "Made by Augustus" before Special thanks, same line rules (kind 3); the name is a code constant, untranslated; an empty list gives no line; the two-line box truncates with "…"
    { cr_line_t ln[CR_LINES]; int tr = 0;
      int n = credits_all(k_maker_default, 1, k_special_default, 1, NULL, 0, ", ", " (note)", ln, CR_LINES, &tr);
      CHECK(n == 2); CHECK(!tr); CHECK(ln[0].kind == 3 && !strcmp(ln[0].text, "Augustus")); CHECK(ln[1].kind == 1 && !strcmp(ln[1].text, "jhaizhou-ops (note)"));
      CHECK(sizeof k_maker_default / sizeof *k_maker_default == 1 && !strcmp(k_maker_default[0], "Augustus")); CHECK(cr_units(ln[0].text) <= CR_LINE_UNITS);
      n = credits_all(k_maker_default, 1, k_special_default, 1, NULL, 0, ", ", "", ln, 2, &tr); CHECK(n == 2 && !tr);   // 提示框两行刚好放下 | the box's two lines fit exactly
      static const char *const th[] = { "x" };
      n = credits_all(k_maker_default, 1, k_special_default, 1, th, 1, ", ", "", ln, 2, &tr); CHECK(n == 2 && tr && strstr(ln[1].text, "…"));
      n = credits_all(NULL, 0, k_special_default, 1, NULL, 0, ", ", "", ln, CR_LINES, &tr); CHECK(n == 1 && ln[0].kind == 1);   // 没有制作名单：不出那一行 | no maker list: no line
      CHECK(!strstr(log_text(), "Augustus")); }   // 名字不进日志（日志只记行数 / 人数） | the name never goes to the log (counts only)
    // 21. 社区 / 关于弹窗（RC1.1 重构）的纵向排版：渠道 0–2 行，卡片高 100、行距 120 不重叠；免费声明在卡片下、分隔线在声明与页脚之间；页脚两行；
    //     末行文字下沿到「确定」顶 = CO_GAP；框高 = 按钮顶 + 115 + 45，按行数算（2 / 1 / 0 行 → 802 / 682 / 562）；横向 标签列 | 值列 | 小「复制」三列不重叠
    // 21. Vertical layout of the redesigned box: 0-2 channel rows, cards 100 high at 120 pitch without overlap; free notice under the cards, divider between it and the footer; two footer lines;
    //     last text bottom to OK top = CO_GAP; height = button top + 115 + 45 by row count (802 / 682 / 562); horizontally label | value | small Copy do not overlap
    for (int n = 0; n <= CO_ROWS; n++) { co_geom_t g = co_geom(n); int bottom = 100;   // 标题文字框 40–100 | title box 40-100
        for (int k = 0; k < n; k++) { CHECK(g.card_y[k] >= bottom + 20); bottom = g.card_y[k] + 100; }
        CHECK(g.free_y >= bottom + 20); CHECK(g.div_y > g.free_y + 60 && g.foot_y[0] > g.div_y); CHECK(g.foot_y[1] == g.foot_y[0] + 48);
        CHECK(g.btn_y - (g.foot_y[1] + 30 + 14) == CO_GAP); CHECK(g.h == g.btn_y + 115 + 45); }
    CHECK(co_geom(2).h == 802 && co_geom(1).h == 682 && co_geom(0).h == 562 && co_geom(5).h == 802);
    CHECK(40 + CO_LABEL_W <= CO_CARD_W / 2 + CO_VALUE_X - 20);                                    // 标签列右缘在值列左边 | label column ends left of the value column
    CHECK(CO_VALUE_X + CO_VALUE_W + 30 == CO_CARD_W / 2 - 30 - CO_BTN_W && CO_VALUE_W >= 800);     // 值列到小按钮留 30 | value column ends 30 left of the small button
    // 截断：按 §2.6 估宽超了列宽就截断加「…」，复制不受影响 | truncation: over the column by the §2.6 estimate → cut with "…"; copy unaffected
    { char t[200]; snprintf(t, sizeof t, "d2rplus.augustusplay.com"); CHECK(!dm_community_fit(t, sizeof t, 36, CO_VALUE_W) && !strcmp(t, "d2rplus.augustusplay.com"));
      snprintf(t, sizeof t, "a-very-long-subdomain-name.example-website.com/path"); CHECK(dm_community_fit(t, sizeof t, 36, CO_VALUE_W)); CHECK(strstr(t, "…"));
      int w = 0; for (const char *q = t; *q; ) { unsigned char c = (unsigned char)*q; w += c < 0x80 ? 36 * 7 / 10 : 36; q += c < 0x80 ? 1 : c >= 0xe0 ? 3 : 2; } CHECK(w <= CO_VALUE_W);
      snprintf(t, sizeof t, "特别鸣谢 甲乙丙丁戊己庚辛壬癸子丑寅卯辰巳午未申酉戌亥甲乙丙丁戊己庚辛壬癸子丑寅卯辰巳午未申酉戌亥"); CHECK(dm_community_fit(t, sizeof t, 28, 560)); CHECK(strstr(t, "…")); }
    // RC1.1 补审 Q11.1-08：dm_community_fit 带目标容量、只走合法 UTF-8 前缀、保证 NUL 结尾（ASan 下也要过：make … CFLAGS 加 -fsanitize=address,undefined）
    // RC1.1 follow-up Q11.1-08: dm_community_fit takes the buffer capacity, walks only a valid UTF-8 prefix and always NUL-terminates (also under ASan)
    { char t[64];
      snprintf(t, sizeof t, "abcde"); CHECK(!dm_community_fit(t, sizeof t, 10, 35) && !strcmp(t, "abcde"));               // 恰好放下（5 × 7 = 35）：不动 | exactly fits: unchanged
      snprintf(t, sizeof t, "abcdef"); CHECK(dm_community_fit(t, sizeof t, 10, 35) && !strcmp(t, "abc\xe2\x80\xa6"));     // 超一个字：退到放得下「…」的边界（21 + 10 ≤ 35） | one over: back to where "…" fits
      snprintf(t, sizeof t, "abcdefghij"); CHECK(dm_community_fit(t, 6, 10, 50) && !strcmp(t, "ab\xe2\x80\xa6"));        // 容量 6：「…」+ NUL 放得下的最远处 | capacity 6: as far as "…" + NUL fit
      snprintf(t, sizeof t, "abcdefghij"); CHECK(dm_community_fit(t, 4, 10, 50) && !strcmp(t, "\xe2\x80\xa6"));            // 容量 4：只剩「…」 | capacity 4: only "…"
      snprintf(t, sizeof t, "abcdefghij"); CHECK(dm_community_fit(t, 3, 10, 50) && !t[0]);                                  // 容量 3：「…」都放不下，空串 | capacity 3: not even "…", empty
      snprintf(t, sizeof t, "abc"); CHECK(dm_community_fit(t, 1, 10, 1000) && !t[0]);                                      // 容量 1：只剩 NUL | capacity 1: NUL only
      snprintf(t, sizeof t, "abc"); CHECK(!dm_community_fit(t, 0, 10, 1000) && !strcmp(t, "abc"));                         // 容量 0：不碰 | capacity 0: untouched
      memset(t, 'a', 8); CHECK(dm_community_fit(t, 8, 10, 1000) && !strcmp(t, "aaaa\xe2\x80\xa6"));                     // 没有 NUL 的缓冲：在容量内收尾并加「…」 | buffer without NUL: ends at cap − 1
      snprintf(t, sizeof t, "abc\xe4\xb8"); CHECK(dm_community_fit(t, sizeof t, 10, 1000) && !strcmp(t, "abc"));           // 截断的多字节序列 | truncated multibyte sequence
      snprintf(t, sizeof t, "ab\x80" "cd"); CHECK(dm_community_fit(t, sizeof t, 10, 1000) && !strcmp(t, "ab"));                // 孤立续字节 | stray continuation byte
      snprintf(t, sizeof t, "a\xc0\xaf" "b"); CHECK(dm_community_fit(t, sizeof t, 10, 1000) && !strcmp(t, "a"));              // 过长编码 | overlong encoding
      snprintf(t, sizeof t, "a\xf8\x88\x80\x80\x80"); CHECK(dm_community_fit(t, sizeof t, 10, 1000) && !strcmp(t, "a"));  // 非法首字节 | invalid lead byte
      snprintf(t, sizeof t, "a\xe4\xb8\x80"); CHECK(dm_community_fit(t, 4, 10, 1000) && !strcmp(t, "a"));                 // 多字节越过容量：不读界外、在字前截 | a multibyte character past capacity: no read beyond, cut before it
      snprintf(t, sizeof t, "\xe4\xb8\x80\xe4\xb8\x80"); CHECK(!dm_community_fit(t, sizeof t, 10, 20) && !strcmp(t, "\xe4\xb8\x80\xe4\xb8\x80"));   // 两个中文字恰好 20 | two CJK characters exactly fit
      snprintf(t, sizeof t, "\xe4\xb8\x80\xe4\xb8\x80\xe4\xb8\x80"); CHECK(dm_community_fit(t, sizeof t, 10, 20) && !strcmp(t, "\xe4\xb8\x80\xe2\x80\xa6")); }   // 多一字：一字 + 「…」 | one more: one character + "…"
    // 22. 官网（用户 2026-10-29：「关于弹窗里网址改成新的官网」）：清单 community.site（https:// 开头）进内存与缓存；链接一项优先官网、没有退回 GitHub；
    //     有官网时「关于」段出 GitHub 行（Q11.1-03：外文界面官网只出现一次），去掉 https:// 与末尾 /；site 不合规当这一项没有并记红；重复当格式错；只有 site 也算有字段；旧缓存（没有 site=）照读
    // 22. Website (user, 2026-10-29): manifest community.site (https://) goes to memory and cache; the link prefers it and falls back to GitHub; with a website the About section shows a GitHub line (Q11.1-03), without https:// and the trailing /;
    //     an invalid site counts as absent with a red line; a duplicate is a format error; site alone is a field; old caches without site= still load
    { static const char *const s1 = "{\"community\": {\"qq_group\": \"1129913678\", \"github\": \"https://github.com/example/d2r-mobile-plus\", \"site\": \"https://d2rplus.example.com/\"}}";
      char lb[200];
      log_reset(); CHECK(dm_community_manifest(s1, strlen(s1)) == 1); CHECK(strstr(log_text(), "官网=https://d2rplus.example.com/"));
      CHECK(dm_community_value(DM_LG_ZH, v, sizeof v) == 1 && !strcmp(v, "1129913678"));
      CHECK(dm_community_value(DM_LG_EN, v, sizeof v) == 2 && !strcmp(v, "https://d2rplus.example.com/"));   // 外文：链接 = 官网 | other languages: link = website
      CHECK(cache_has("site=https://d2rplus.example.com/\n"));
      restart(); dm_community_load(); CHECK(dm_community_value(DM_LG_RU, v, sizeof v) == 2 && !strcmp(v, "https://d2rplus.example.com/")); CHECK(strstr(log_text(), "官网=https://d2rplus.example.com/"));
      static const char *const s2 = "{\"community\": {\"github\": \"https://github.com/example/d2r-mobile-plus\", \"site\": \"http://d2rplus.example.com/\"}}";   // 不是 https | not https
      log_reset(); CHECK(dm_community_manifest(s2, strlen(s2)) == 1); CHECK(strstr(log_text(), "另一项格式坏"));
      CHECK(dm_community_value(DM_LG_EN, v, sizeof v) == 2 && !strcmp(v, "https://github.com/example/d2r-mobile-plus"));   // 退回 GitHub | falls back to GitHub
      CHECK(chs(DM_LG_EN, lb) == 1 && lb[0] == DM_CH_GITHUB); CHECK(!cache_has("site="));
      static const char *const s3 = "{\"community\": {\"site\": \"https://a.example\", \"site\": \"https://b.example\"}}";
      log_reset(); CHECK(dm_community_manifest(s3, strlen(s3)) == 0); CHECK(strstr(log_text(), "site 重复")); CHECK(!dm_community_available());
      static const char *const s4 = "{\"community\": {\"site\": \"https://d2rplus.example.com/path\"}}";   // 只有官网：外文显示、中文也显示链接 | site only: every language shows it
      CHECK(dm_community_manifest(s4, strlen(s4)) == 1); CHECK(dm_community_value(DM_LG_ZH, v, sizeof v) == 2 && !strcmp(v, "https://d2rplus.example.com/path"));
      CHECK(chs(DM_LG_EN, lb) == 1 && lb[0] == DM_CH_SITE && chs(DM_LG_ZH, lb) == 1 && lb[0] == DM_CH_SITE);   // 只有官网：中外文都是一行官网 | website only: one website row in every language
      static const char *const s5 = "{\"community\": {\"site\": \"https:///x\"}}";   // 没有主机名 | no host
      CHECK(dm_community_manifest(s5, strlen(s5)) == 0);
      // Q11-01 口径：带转义的 site 当这一项没有（链接退回 GitHub）；嵌套更深的同名 site 不认；site 不是字符串当格式坏 | Q11-01: an escaped site counts as absent (link falls back); a deeper same-name site is ignored; a non-string site is malformed
      static const char *const s6 = "{\"community\": {\"github\": \"https://github.com/example/d2r-mobile-plus\", \"site\": \"https:\\/\\/d2rplus.example.com\"}}";
      log_reset(); CHECK(dm_community_manifest(s6, strlen(s6)) == 1); CHECK(strstr(log_text(), "另一项格式坏")); CHECK(chs(DM_LG_EN, lb) == 1 && lb[0] == DM_CH_GITHUB);
      CHECK(dm_community_value(DM_LG_EN, v, sizeof v) == 2 && !strcmp(v, "https://github.com/example/d2r-mobile-plus"));
      static const char *const s7 = "{\"community\": {\"github\": \"https://github.com/example/d2r-mobile-plus\", \"extra\": {\"site\": \"https://deep.example\"}}, \"site\": \"https://root.example\"}";
      CHECK(dm_community_manifest(s7, strlen(s7)) == 1); CHECK(chs(DM_LG_EN, lb) == 1 && lb[0] == DM_CH_GITHUB); CHECK(!cache_has("site="));
      static const char *const s8 = "{\"community\": {\"github\": \"https://github.com/example/d2r-mobile-plus\", \"site\": 42}}";
      log_reset(); CHECK(dm_community_manifest(s8, strlen(s8)) == 1); CHECK(strstr(log_text(), "另一项格式坏")); CHECK(chs(DM_LG_EN, lb) == 1 && lb[0] == DM_CH_GITHUB);
      // RC1.1 弹窗重构 / 补审 Q11.1-06：渠道行——中文 [QQ 群, 官网, GitHub] 取前两个有值的，外文 [官网, GitHub]；显示值去 https:// 与末尾 /、GitHub 只留仓库名；每行的「复制」复制那一行的完整值
      // RC1.1 box redesign / follow-up Q11.1-06: channel rows -- Chinese the first two of [QQ, website, GitHub], other languages [website, GitHub]; shown values trimmed, GitHub as repo name; each row's Copy copies that row's full value
      { CHECK(dm_community_manifest(s1, strlen(s1)) == 1); int c[2]; char full[200], shown[200];
        CHECK(dm_community_channels(DM_LG_ZH, c) == 2 && c[0] == DM_CH_QQ && c[1] == DM_CH_SITE);                 // 中文：群号 + 官网（Q11.1-06：中文看得到官网） | Chinese: number + website
        CHECK(dm_community_channels(DM_LG_EN, c) == 2 && c[0] == DM_CH_SITE && c[1] == DM_CH_GITHUB);             // 外文：官网 + GitHub，不显示群号 | other languages: website + GitHub, no number
        CHECK(dm_community_channels(DM_LG_KO, c) == 2 && c[0] == DM_CH_SITE);
        CHECK(dm_community_channel_value(DM_CH_SITE, full, sizeof full, shown, sizeof shown) && !strcmp(full, "https://d2rplus.example.com/") && !strcmp(shown, "d2rplus.example.com"));
        CHECK(dm_community_channel_value(DM_CH_GITHUB, full, sizeof full, shown, sizeof shown) && !strcmp(full, "https://github.com/example/d2r-mobile-plus") && !strcmp(shown, "d2r-mobile-plus"));
        CHECK(dm_community_channel_value(DM_CH_QQ, full, sizeof full, shown, sizeof shown) && !strcmp(full, "1129913678") && !strcmp(shown, "1129913678"));
        // RC1.1 终版审计 P2-4：显示 owner/repo 的 repo 段，不是路径最后一段；复制仍是完整地址 | final audit P2-4: show the repo segment of owner/repo, not the last path segment; copy keeps the full URL
        { static const struct { const char *url, *want; } gh_cases[] = { { "https://github.com/example/d2r-mobile-plus/discussions", "d2r-mobile-plus" }, { "https://github.com/example/d2r-mobile-plus/", "d2r-mobile-plus" },
              { "https://github.com/example/d2r-mobile-plus/releases/tag/v1", "d2r-mobile-plus" }, { "https://github.com/example", "example" } };
          for (size_t k = 0; k < sizeof gh_cases / sizeof *gh_cases; k++) { char mj[512]; snprintf(mj, sizeof mj, "{\"community\": {\"github\": \"%s\"}}", gh_cases[k].url);
              if (dm_community_manifest(mj, strlen(mj)) != 1) { CHECK(!"manifest"); continue; }
              CHECK(dm_community_channel_value(DM_CH_GITHUB, full, sizeof full, shown, sizeof shown) && !strcmp(full, gh_cases[k].url) && !strcmp(shown, gh_cases[k].want)); }
          CHECK(dm_community_manifest(s1, strlen(s1)) == 1); }   // 换回本段的清单 | back to this block's manifest
        dm_community_copy_init(copy_ok); atomic_store(&g_show, 2); unsigned c0 = dm_community_copy_count();
        t_group = DM_LG_ZH; log_reset(); dm_community_on_uimsg("PanelManager", "ClosePanel", "TouchCommCopy1"); CHECK(!strcmp(copied, "https://d2rplus.example.com/")); CHECK(strstr(log_text(), "社区入口：复制 官网"));
        dm_community_on_uimsg("PanelManager", "ClosePanel", "TouchCommCopy0"); CHECK(!strcmp(copied, "1129913678")); CHECK(atomic_load(&g_copied_row) == 0);
        t_group = DM_LG_EN; dm_community_on_uimsg("PanelManager", "ClosePanel", "TouchCommCopy1"); CHECK(!strcmp(copied, "https://github.com/example/d2r-mobile-plus")); CHECK(atomic_load(&g_copied_row) == 1);
        CHECK(dm_community_copy_count() == c0 + 3);
        dm_community_on_uimsg("PanelManager", "ClosePanel", "TouchCommCopy2"); dm_community_on_uimsg("PanelManager", "ClosePanel", "TouchCommCopy10"); CHECK(dm_community_copy_count() == c0 + 3);   // 没有第三行 | no third row
        static const char *const s10 = "{\"community\": {\"qq_group\": \"1129913678\", \"github\": \"https://github.com/example/d2r-mobile-plus\"}}";   // 没有官网：中文 群号 + GitHub、外文 GitHub 一行（与 RC1 同值） | no website
        CHECK(dm_community_manifest(s10, strlen(s10)) == 1); CHECK(dm_community_channels(DM_LG_ZH, c) == 2 && c[1] == DM_CH_GITHUB); CHECK(dm_community_channels(DM_LG_EN, c) == 1 && c[0] == DM_CH_GITHUB);
        log_reset(); dm_community_on_uimsg("PanelManager", "ClosePanel", "TouchCommCopy1"); CHECK(strstr(log_text(), "【红】社区入口：复制失败（第 2 行"));   // 外文只有一行：第二行的复制不该有 | other languages have one row
        atomic_store(&g_show, 0); t_group = DM_LG_ZH; }
      // RC1.1 增量审计 Q11.1-01 / 02：site 收紧——拒绝 userinfo（@）、? 与 #、空 / 坏主机名；主机名只许 [A-Za-z0-9.-]、要有点、不以点或横线开头结尾；可选路径；B024 起不收端口（主机名后只能是串尾或 /，整串不许 :）；总长 ≤ 60（值行 36 号估宽）
      // RC1.1 review Q11.1-01 / 02: site tightened -- rejects userinfo (@), ? and #, empty / bad hosts; host [A-Za-z0-9.-] with a dot, not starting or ending with . or -; optional path; from B024 no port (host followed only by end or /, no : anywhere); at most 60 characters
      { static const char *const bad[] = { "https://?x", "https://#", "https://d2rplus.example.com@evil.example/", "https://a.b@c.d/", "https://a.example/x?y=1", "https://a.example/#top",
            "https://localhost/", "https://.a.example/", "https://a.example./", "https://-a.example/", "https://a.example-/", "https://a_b.example/", "https://a.example:/", "https://a.example:123456/",
            "https://a.example:80x/", "https://:443/", "https://a.example:443?x",
            "https://a.example:443/", "https://a.example:443", "https://a-b.example.com:8443/d2r/v1/", "https://a.example/x:y", "https://a.example:" };   // B024：端口 / 路径里的 : 都拒 | B024: a port or a : in the path is rejected
        for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) CHECK(!site_ok(bad[i]));
        static const char *const good[] = { "https://d2rplus.augustusplay.com/", "https://a.example", "https://a-b.example.com/d2r/v1/", "https://A1.Example.com/p_a-t.h~/(x)" };
        for (size_t i = 0; i < sizeof good / sizeof *good; i++) CHECK(site_ok(good[i]));
        char u[80]; snprintf(u, sizeof u, "https://a.example/%0*d", SITE_MAX - 18, 0); CHECK(strlen(u) == SITE_MAX && site_ok(u));   // 恰 60 收 | exactly 60 accepted
        snprintf(u, sizeof u, "https://a.example/%0*d", SITE_MAX - 17, 0); CHECK(strlen(u) == SITE_MAX + 1 && !site_ok(u));       // 61 拒 | 61 rejected
        char m[256]; snprintf(m, sizeof m, "{\"community\": {\"github\": \"https://github.com/example/d2r-mobile-plus\", \"site\": \"%s\"}}", u);
        log_reset(); CHECK(dm_community_manifest(m, strlen(m)) == 1); CHECK(strstr(log_text(), "另一项格式坏"));   // 超长：丢这一项、退回 GitHub | too long: dropped, GitHub fallback
        CHECK(dm_community_value(DM_LG_EN, v, sizeof v) == 2 && !strcmp(v, "https://github.com/example/d2r-mobile-plus")); CHECK(!cache_has("site="));
        static const char *const s9 = "{\"community\": {\"github\": \"https://github.com/example/d2r-mobile-plus\", \"site\": \"https://d2rplus.example.com@evil.example/\"}}";
        log_reset(); CHECK(dm_community_manifest(s9, strlen(s9)) == 1); CHECK(chs(DM_LG_EN, lb) == 1 && lb[0] == DM_CH_GITHUB); CHECK(strstr(log_text(), "官网=无"));
        // B024：带端口的官网——清单里丢这一项（记红、退回 GitHub、不进缓存）；手写进缓存的也不认 | B024: a website with a port -- dropped from the manifest (red, GitHub fallback, not cached); not accepted from a hand-written cache either
        static const char *const s11 = "{\"community\": {\"github\": \"https://github.com/example/d2r-mobile-plus\", \"site\": \"https://a.example:443/\"}}";
        log_reset(); CHECK(dm_community_manifest(s11, strlen(s11)) == 1); CHECK(strstr(log_text(), "另一项格式坏")); CHECK(strstr(log_text(), "官网=无"));
        CHECK(chs(DM_LG_EN, lb) == 1 && lb[0] == DM_CH_GITHUB); CHECK(dm_community_value(DM_LG_EN, v, sizeof v) == 2 && !strcmp(v, "https://github.com/example/d2r-mobile-plus")); CHECK(!cache_has("site="));
        static const char *const s12 = "{\"community\": {\"site\": \"https://a.example:443/\"}}";   // 只有它：整个字段当格式错 | the only key: the whole field is malformed
        log_reset(); CHECK(dm_community_manifest(s12, strlen(s12)) == 0); CHECK(!dm_community_available());
        FILE *fp = fopen(cache, "w"); fputs("github=https://github.com/example/d2r-mobile-plus\nsite=https://a.example:443/\n", fp); fclose(fp);
        restart(); log_reset(); dm_community_load(); CHECK(strstr(log_text(), "社区入口：缓存 ")); CHECK(strstr(log_text(), "官网=无")); CHECK(chs(DM_LG_EN, lb) == 1 && lb[0] == DM_CH_GITHUB);
        CHECK(dm_community_value(DM_LG_EN, v, sizeof v) == 2 && !strcmp(v, "https://github.com/example/d2r-mobile-plus"));
        fp = fopen(cache, "w"); fputs("site=https://d2rplus.example.com/\n", fp); fclose(fp);   // 对照：同路径不带端口的缓存照读 | control: the same cache without a port loads
        restart(); dm_community_load(); CHECK(chs(DM_LG_EN, lb) == 1 && lb[0] == DM_CH_SITE); }
      FILE *f = fopen(cache, "w"); fputs("qq_group=1129913678\ngithub=https://github.com/example/d2r-mobile-plus\n", f); fclose(f);   // 旧缓存（RC1 写的） | an RC1 cache
      restart(); dm_community_load(); CHECK(dm_community_value(DM_LG_EN, v, sizeof v) == 2 && !strcmp(v, "https://github.com/example/d2r-mobile-plus")); CHECK(chs(DM_LG_EN, lb) == 1 && lb[0] == DM_CH_GITHUB); }
    // 23. RC1.1（用户 Duo 真机 80c0882「加入社群讨论都歪到哪」）：入口先藏后显（新安装首启：清单还没到）时放回挪走前的 x，不放回写死的值——
    //     触屏首页「加入社区讨论」布局里居中 x −235，旧代码放回旧并排版式的 20（往右歪约 255）；还没挪过（home = 99999）就不动
    // 23. RC1.1 (user on the Duo, 80c0882: the Join button was off-centre): an entry hidden then shown (fresh install's first run, manifest not yet in) returns to its x before the move,
    //     not to a hard-coded one -- the touch front page's Join is centred at x −235, the old code put it back at 20; never moved (home = 99999) means leave it alone
    { static uint8_t wb2[0x200]; static void (*vt2[4])(uint64_t); vt2[2] = t_recalc; uint64_t w = (uint64_t)(uintptr_t)wb2; *(uint64_t *)w = (uint64_t)(uintptr_t)vt2; *(const char **)(w + 8) = "TouchCommunityFe";
      int32_t *r = (int32_t *)(w + 0x70), home = 99999; r[0] = -235; r[1] = 1891;
      co_entry_show(w, 1, &home); CHECK(r[0] == -235 && home == 99999);              // 有值、在屏上：不动 | shown already: unchanged
      co_entry_show(w, 0, &home); CHECK(r[0] == 99999 && r[1] == 1891 && home == -235);   // 没值：挪出屏、记下原 x | no value: off screen, x remembered
      co_entry_show(w, 0, &home); CHECK(r[0] == 99999 && home == -235);
      co_entry_show(w, 1, &home); CHECK(r[0] == -235 && r[1] == 1891);              // 有值了：回到原位（旧代码是 20） | value arrives: back to its own x (old code: 20)
      r[0] = 99999; home = 99999; co_entry_show(w, 1, &home); CHECK(r[0] == 99999);   // 没记过原位：不乱放 | no remembered x: not placed anywhere
      co_entry_show(0, 1, &home); }
    // 24. RC1.1 阻塞（用户 Duo 真机 80c0882：触屏穿透到暂停菜单、左摇杆还在动左边菜单）：弹窗开着时取手柄处闸门把键与两根摇杆全清零，方向键与左 / 右摇杆的上下
    //     都变成焦点移动（摇杆带回滞，推住不连发），A / B 留给弹窗，其余吞掉并计数；关着时原样放过；点按（无游戏模块时）只计数不崩
    // 24. RC1.1 blocker: while the box is up the controller gate zeroes every key and both sticks; D-pad and either stick's up / down become focus moves (stick with hysteresis, no repeat while held),
    //     A / B go to the box, the rest is swallowed and counted; closed = pass-through; a tap without a game module only counts and does not crash
    { uint64_t bt; int32_t lx, ly, rx, ry; atomic_store(&g_md_ev, 0); atomic_store(&g_sw_key, 0); atomic_store(&g_sw_stick, 0); atomic_store(&g_sw_touch, 0); atomic_store(&g_leak, 0);
      atomic_store(&g_show, 0); bt = DM_BTN_A | DM_BTN_LEFT; lx = 100; ly = 30000; rx = ry = 0; dm_community_input_gate(&bt, &lx, &ly, &rx, &ry);
      CHECK(bt == (DM_BTN_A | DM_BTN_LEFT) && ly == 30000 && lx == 100 && !atomic_load(&g_md_ev));   // 关着：原样放过 | closed: untouched
      atomic_store(&g_show, 2);
      bt = DM_BTN_A | DM_BTN_PLUS | DM_BTN_L | DM_BTN_ZR | DM_BTN_LEFT; lx = 20000; ly = 0; rx = 5000; ry = 0; dm_community_input_gate(&bt, &lx, &ly, &rx, &ry);
      CHECK(!bt && !lx && !ly && !rx && !ry); CHECK(atomic_load(&g_md_ev) == 0);   // 全清零；打开时就按着的 A（点开弹窗的那一下）不算新按下 | all zero; the A held since opening (the press that opened it) is not a new press
      bt = 0; dm_community_input_gate(&bt, &lx, &ly, &rx, &ry); bt = DM_BTN_A; dm_community_input_gate(&bt, &lx, &ly, &rx, &ry); CHECK(atomic_load(&g_md_ev) == DM_BTN_A && !bt);   // 松开再按：A 攒给弹窗 | release then press: A for the box
      bt = DM_BTN_A; dm_community_input_gate(&bt, &lx, &ly, &rx, &ry); CHECK(atomic_exchange(&g_md_ev, 0) == DM_BTN_A);   // 按住不算新的按下 | held is not a new press
      bt = 0; ly = 30000; lx = ry = rx = 0; dm_community_input_gate(&bt, &lx, &ly, &rx, &ry); CHECK(atomic_load(&g_md_ev) == DM_BTN_UP && !ly);   // 左摇杆上 = 焦点上移 | left stick up = focus up
      ly = 30000; dm_community_input_gate(&bt, &lx, &ly, &rx, &ry); ly = 12000; dm_community_input_gate(&bt, &lx, &ly, &rx, &ry); CHECK(atomic_exchange(&g_md_ev, 0) == DM_BTN_UP);   // 推住 / 回到半中间：不连发 | held / half back: no repeat
      ly = 0; dm_community_input_gate(&bt, &lx, &ly, &rx, &ry); ly = -30000; dm_community_input_gate(&bt, &lx, &ly, &rx, &ry); CHECK(atomic_exchange(&g_md_ev, 0) == DM_BTN_DOWN);   // 回中再下推 | centre then down
      ly = 0; dm_community_input_gate(&bt, &lx, &ly, &rx, &ry); ry = 30000; dm_community_input_gate(&bt, &lx, &ly, &rx, &ry); CHECK(atomic_exchange(&g_md_ev, 0) == DM_BTN_UP && !ry);   // 右摇杆同样 | right stick likewise
      ry = 0; lx = 30000; dm_community_input_gate(&bt, &lx, &ly, &rx, &ry); CHECK(!atomic_load(&g_md_ev) && !lx);   // 左右：吞掉，无处可去 | left / right: swallowed, nowhere to go
      bt = DM_BTN_DOWN | DM_BTN_B; lx = 0; dm_community_input_gate(&bt, &lx, &ly, &rx, &ry); CHECK(atomic_exchange(&g_md_ev, 0) == (DM_BTN_DOWN | DM_BTN_B) && !bt);
      CHECK(atomic_load(&g_sw_key) == 3 && atomic_load(&g_sw_stick) == 3);
      dm_community_input_leak(0); CHECK(atomic_load(&g_leak) == 0); dm_community_input_leak(1); CHECK(atomic_load(&g_leak) == 1);   // 闸门后还有输入 = 漏（计数） | input after the gate = leak (counted)
      dm_community_ui_tap(0, 100, 100); CHECK(atomic_load(&g_sw_touch) == 1);                        // 没有游戏界面：只计数 | no game UI: counted only
      CHECK(dm_community_modal()); atomic_store(&g_show, 0); CHECK(!dm_community_modal());
      // RC1.1 终版审计 P2-1：手柄 B（或 A）关框的那一拍弹窗已关、键还按着——松开之前整只手柄都不交给底层，压住的拍数清零前计数；全松开后照常放过并记一行
      // final audit P2-1: on the read where B (or A) closes the box the key is still held -- the whole controller is withheld until release, counted before zeroing; afterwards pass-through and one log line
      atomic_store(&g_leak, 0); atomic_store(&g_after_n, 0);
      bt = DM_BTN_B; lx = ly = rx = ry = 0; dm_community_input_gate(&bt, &lx, &ly, &rx, &ry); CHECK(bt == 0);   // 关框那一下的 B 还按着：不交 | the closing B still held: withheld
      bt = DM_BTN_B | DM_BTN_A; lx = 20000; dm_community_input_gate(&bt, &lx, &ly, &rx, &ry); CHECK(bt == 0 && lx == 0);   // 窗口里新按的也不交 | fresh input inside the window withheld too
      dm_community_input_leak(bt || lx || ly || rx || ry); CHECK(atomic_load(&g_leak) == 0);
      dm_community_input_leak(1); CHECK(atomic_load(&g_leak) == 1); atomic_store(&g_leak, 0);   // 窗口里闸门后还有输入 = 漏（原先关框后恒不计） | input after the gate inside the window = leak (never counted before)
      CHECK(atomic_load(&g_after_n) == 2 && atomic_load(&g_after));
      log_reset(); bt = 0; lx = 0; dm_community_input_gate(&bt, &lx, &ly, &rx, &ry); CHECK(!atomic_load(&g_after)); CHECK(strstr(log_text(), "关框后压住按着的键 / 摇杆 2 拍"));   // 全松开：窗口结束 | all released: window ends
      bt = DM_BTN_B; ly = 30000; dm_community_input_gate(&bt, &lx, &ly, &rx, &ry); CHECK(bt == DM_BTN_B && ly == 30000);   // 之后照常放过 | afterwards pass-through
      dm_community_input_leak(1); CHECK(atomic_load(&g_leak) == 0);                                  // 窗口外不算漏 | outside the window not a leak
      // 摇杆推着关框：回到 1/4 以内之前不交 | closing with a stick pushed: withheld until it is back inside a quarter
      atomic_store(&g_show, 2); bt = 0; lx = 0; ly = -30000; dm_community_input_gate(&bt, &lx, &ly, &rx, &ry); atomic_store(&g_show, 0);
      bt = 0; ly = -12000; dm_community_input_gate(&bt, &lx, &ly, &rx, &ry); CHECK(ly == 0);
      ly = -5000; dm_community_input_gate(&bt, &lx, &ly, &rx, &ry); CHECK(ly == -5000 && !atomic_load(&g_after));
      atomic_store(&g_md_ev, 0); }
    // 25. RC1.1 复核 P1-2：弹窗开着时整条触屏链——宿主命中答案（dm_community_hit_answer，dm_cursor.c 两种查询都取它）→ 手势层（dm_touch_controls.c，iOS 与 Mac 共用）
    //     → 前端胶水（front_ios.m：鼠标点击序号变了就 dm_ui_touch 按下 + 抬起）→ 鼠标泵的弹窗分支（dm_cursor.c：弹窗开着时抬起交 dm_community_ui_tap）→ 点中「确定」收起。
    //     假控件树：根 → 暂停菜单面板 → 标题 + 「确定」（虚表 +0x48 = 按矩形命中）。旧答案 6 在同一条链上什么都不发（9943fd2 的失败），也钉住
    // 25. RC1.1 recheck P1-2: the whole touch chain while the box is up -- the host hit answer (dm_community_hit_answer, taken by both of dm_cursor.c's queries) -> the gesture layer
    //     (dm_touch_controls.c, shared by iOS and Mac) -> the front-end glue (front_ios.m: when the mouse click sequence moved, dm_ui_touch down + up) -> the mouse pump's box branch
    //     (dm_cursor.c: while the box is up the lift goes to dm_community_ui_tap) -> OK hit, box dismissed. Fake tree: root -> pause panel -> title + OK (vtable +0x48 = rect hit test).
    //     The old answer 6 sends nothing down the same chain (the 9943fd2 failure), pinned as well
    { static uint8_t wb[4][0x200]; static uint64_t kids_root[1], kids_panel[2]; static uint64_t vt[16];
      vt[9] = (uint64_t)(uintptr_t)t_rect_hit;   // +0x48 = 第 9 个槽 | +0x48 = slot 9
      uint64_t root = (uint64_t)(uintptr_t)wb[0], panel = (uint64_t)(uintptr_t)wb[1], title = (uint64_t)(uintptr_t)wb[2], ok = (uint64_t)(uintptr_t)wb[3];
      const char *names[4] = { "Root", "PauseLayout", "TouchCommTitle", "TouchCommOk" };
      for (int k = 0; k < 4; k++) { *(uint64_t *)(uintptr_t)(wb[k]) = (uint64_t)(uintptr_t)vt; *(const char **)(wb[k] + 8) = names[k]; wb[k][0x50] = wb[k][0x51] = 1; }
      kids_root[0] = panel; *(uint64_t *)(root + 0x58) = (uint64_t)(uintptr_t)kids_root; *(uint64_t *)(root + 0x60) = 1;
      kids_panel[0] = title; kids_panel[1] = ok; *(uint64_t *)(panel + 0x58) = (uint64_t)(uintptr_t)kids_panel; *(uint64_t *)(panel + 0x60) = 2;
      int32_t *okr = (int32_t *)(uintptr_t)(ok + 0x70); okr[0] = 1300; okr[1] = 1050; okr[2] = 260; okr[3] = 80;   // 「确定」在游戏窗口像素 (1300,1050) 260×80 | OK at game pixels (1300,1050) 260x80
      for (int pass = 0; pass < 2; pass++) {
          atomic_store(&g_show, 2); atomic_store(&g_ack, 0); atomic_store(&g_sw_touch, 0);
          int answer = pass == 0 ? dm_community_hit_answer() : 6;   // 第二遍：9943fd2 的旧答案 | second pass: the old answer from 9943fd2
          if (pass == 0) CHECK(answer == 0);
          dm_tc_state s; dm_tc_init(&s, 2868, 1320); dm_tc_set_navigation_mode(&s, 1); s.mouse_click_mode = 1;
          unsigned mc = s.mouse_click_seq; float tx = 1430, ty = 1090;   // 点在「确定」中间（手势层坐标 = 游戏窗口像素，同 touch_mac.c） | tap the middle of OK
          dm_tc_begin_at(&s, 0, tx, ty, 10.0); dm_tc_pointer_item(&s, answer); dm_tc_end_at(&s, 0, 10.08);
          if (s.mouse_click_seq != mc && dm_community_modal()) dm_community_ui_tap(root, (int)s.mouse_click_x, (int)s.mouse_click_y);   // 前端胶水 + 鼠标泵的弹窗分支 | front-end glue + the mouse pump's box branch
          if (pass == 0) { CHECK(s.mouse_click_seq == mc + 1); CHECK(atomic_load(&g_sw_touch) == 1); CHECK(atomic_load(&g_ack) == 1); }   // 点到了「确定」：收起 | OK hit: dismissed
          else { CHECK(s.mouse_click_seq == mc); CHECK(atomic_load(&g_sw_touch) == 0); CHECK(atomic_load(&g_ack) == 0); } }   // 答 6：弹窗收不到点按（复核 P1-2 的现象） | answer 6: the box never sees the tap
      atomic_store(&g_show, 0); atomic_store(&g_ack, 0); CHECK(dm_community_hit_answer() == -1); }
    // 15. 没有 UI（单测没有游戏模块）：每帧泵不崩 | 15. No UI (no game module in the unit test): the per-frame pump does not crash
    atomic_store(&g_show, 1); dm_community_pump(); dm_community_pump(); atomic_store(&g_show, 0);
    unlink(cache); unlink(logp); { char mp[160]; snprintf(mp, sizeof mp, "%s/sample.json", dir); unlink(mp); } rmdir(dir);
    fflush(stderr); printf("t_community: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
