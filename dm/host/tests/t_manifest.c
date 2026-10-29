// 远端更新清单严格解析（dm_manifest.c）回归：样例清单（与 fork 仓库 docs/latest.json 同字段）通过；必填缺失 / 类型错 / 取值越界 / 重复键 /
// 语法错（尾逗号、根后多余、非对象根、深度超限）/ 超长 / BOM / 社区字段转义都整份拒绝；未知键与嵌套同名键忽略；community 可省。
// Regression for strict manifest parsing (dm_manifest.c): the sample manifest (same fields as docs/latest.json in the fork) passes; missing required fields / wrong types /
// out-of-range values / duplicate keys / syntax errors (trailing comma, trailing content, non-object root, too deep) / oversize / BOM / escapes in community fields reject the
// whole manifest; unknown keys and nested same-name keys are ignored; community is optional.
#include "../dm_manifest.c"
#include <stdio.h>
#include <string.h>
static int checks, fails;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
static int ok(const char *s) { dm_manifest m; char why[160]; return dm_manifest_parse(s, strlen(s), &m, why, sizeof why); }
static int no(const char *s) { char why[160]; int r = dm_manifest_parse(s, strlen(s), NULL, why, sizeof why); if (!r && !why[0]) { printf("FAIL: no reason for %s\n", s); fails++; } return !r; }
#define BASE "\"build\": 1793254654, \"note\": \"1.0.0 RC1\", \"urgent\": false"

int main(void) {
    dm_manifest m; char why[160];
    const char *sample = "{\n  \"build\": 1793254654,\n  \"version\": \"1.0.0\",\n  \"note\": \"1.0.0 RC1：光追、内存、卡死修复、字体\",\n  \"urgent\": false,\n"
                         "  \"community\": {\n    \"qq_group\": \"1129913678\",\n    \"github\": \"https://github.com/augustxxx-debug/d2r-mobile-plus\"\n  }\n}\n";
    CHECK(dm_manifest_parse(sample, strlen(sample), &m, why, sizeof why) == 1); CHECK(why[0] == 0);
    CHECK(m.build == 1793254654); CHECK(!strcmp(m.version, "1.0.0")); CHECK(m.urgent == 0); CHECK(!strcmp(m.qq, "1129913678"));
    CHECK(!strcmp(m.github, "https://github.com/augustxxx-debug/d2r-mobile-plus")); CHECK(strstr(m.note, "RC1") != NULL);
    // 可省：version / community / 社区里任一项；未知键、嵌套同名键忽略 | optional: version / community / either community item; unknown and nested same-name keys ignored
    CHECK(ok("{" BASE "}")); CHECK(dm_manifest_parse("{" BASE "}", strlen("{" BASE "}"), &m, NULL, 0) && !m.qq[0] && !m.github[0] && !m.version[0]);
    CHECK(ok("{" BASE ", \"community\": {}}"));
    CHECK(ok("{" BASE ", \"community\": {\"qq_group\": \"12345\"}}"));
    CHECK(ok("{" BASE ", \"community\": {\"github\": \"https://github.com/a/b\"}}"));
    CHECK(ok("{" BASE ", \"extra\": {\"build\": \"x\", \"community\": 1, \"a\": [1, 2.5e3, null, true]}}"));
    CHECK(ok("{" BASE ", \"credits\": {\"special\": [\"jhaizhou-ops\"], \"thanks\": []}}"));
    CHECK(ok("{" BASE ", \"render_safe\": true}")); { const char *s = "{" BASE ", \"render_safe\": true}"; dm_manifest_parse(s, strlen(s), &m, NULL, 0); CHECK(m.render_safe == 1); }
    CHECK(ok("{" BASE ", \"urgent2\": 1}"));
    CHECK(ok("{\"build\": 1, \"note\": \"\", \"urgent\": true}")); CHECK(ok("{\"build\": 1999999999, \"note\": \"\\u4e2d\", \"urgent\": true}"));
    // 必填缺失 / 类型错 | missing required / wrong type
    CHECK(no("{\"note\": \"x\", \"urgent\": false}")); CHECK(no("{\"build\": 1, \"urgent\": false}")); CHECK(no("{\"build\": 1, \"note\": \"x\"}"));
    CHECK(no("{\"build\": \"1\", \"note\": \"x\", \"urgent\": false}")); CHECK(no("{\"build\": 1, \"note\": 1, \"urgent\": false}"));
    CHECK(no("{\"build\": 1, \"note\": \"x\", \"urgent\": 0}")); CHECK(no("{\"build\": 1, \"note\": \"x\", \"urgent\": \"false\"}"));
    CHECK(no("{" BASE ", \"render_safe\": 1}"));
    // build 取值 | build value
    CHECK(no("{\"build\": 0, \"note\": \"x\", \"urgent\": false}")); CHECK(no("{\"build\": -5, \"note\": \"x\", \"urgent\": false}"));
    CHECK(no("{\"build\": 2000000000, \"note\": \"x\", \"urgent\": false}")); CHECK(no("{\"build\": 99999999999999999999, \"note\": \"x\", \"urgent\": false}"));
    CHECK(no("{\"build\": 1.5, \"note\": \"x\", \"urgent\": false}")); CHECK(no("{\"build\": 1e3, \"note\": \"x\", \"urgent\": false}"));
    CHECK(no("{\"build\": 01, \"note\": \"x\", \"urgent\": false}"));
    // version | version
    CHECK(ok("{" BASE ", \"version\": \"1.0.0+28-rc.1\"}")); CHECK(no("{" BASE ", \"version\": \"\"}")); CHECK(no("{" BASE ", \"version\": \"1.0 beta\"}"));
    CHECK(no("{" BASE ", \"version\": \"123456789012345678901\"}")); CHECK(ok("{" BASE ", \"version\": \"12345678901234567890\"}"));
    CHECK(no("{" BASE ", \"version\": 1}")); CHECK(no("{" BASE ", \"version\": \"1\\u002e0\"}"));
    // community 与两项 | community and its items
    CHECK(no("{" BASE ", \"community\": \"1129913678\"}")); CHECK(no("{" BASE ", \"community\": null}"));
    CHECK(no("{" BASE ", \"community\": {\"qq_group\": 1129913678}}")); CHECK(no("{" BASE ", \"community\": {\"qq_group\": \"1234\"}}"));
    CHECK(no("{" BASE ", \"community\": {\"qq_group\": \"1234567890123\"}}")); CHECK(no("{" BASE ", \"community\": {\"qq_group\": \"12345a\"}}"));
    CHECK(no("{" BASE ", \"community\": {\"qq_group\": \"\\u0031\\u0032\\u0033\\u0034\\u0035\"}}"));
    CHECK(no("{" BASE ", \"community\": {\"github\": \"http://github.com/a/b\"}}")); CHECK(no("{" BASE ", \"community\": {\"github\": \"https://github.com/\"}}"));
    CHECK(no("{" BASE ", \"community\": {\"github\": \"https://github.com.evil.example/a\"}}"));   // 前缀必须是 https://github.com/（带斜杠） | the prefix must be https://github.com/ (with the slash)
    CHECK(no("{" BASE ", \"community\": {\"github\": \"https://github.com/a b\"}}")); CHECK(no("{" BASE ", \"community\": {\"github\": \"https://github.com/a\\/b\"}}"));
    { char s[400]; snprintf(s, sizeof s, "{" BASE ", \"community\": {\"github\": \"https://github.com/%0142d\"}}", 0); CHECK(no(s)); }   // 19 + 142 = 161 字符 | 161 characters
    { char s[400]; snprintf(s, sizeof s, "{" BASE ", \"community\": {\"github\": \"https://github.com/%0141d\"}}", 0); CHECK(ok(s)); }   // 160 字符 | 160 characters
    // 重复键 | duplicate keys
    CHECK(no("{" BASE ", \"build\": 2}")); CHECK(no("{" BASE ", \"urgent\": true}"));
    CHECK(no("{" BASE ", \"community\": {}, \"community\": {}}")); CHECK(no("{" BASE ", \"community\": {\"qq_group\": \"12345\", \"qq_group\": \"67890\"}}"));
    CHECK(ok("{" BASE ", \"community\": {\"x\": {\"qq_group\": 1, \"qq_group\": 2}}}"));   // 嵌套同名不算 | nested same name does not count
    // 语法 | syntax
    CHECK(no("{" BASE ",}")); CHECK(no("{" BASE "} x")); CHECK(no("[{" BASE "}]")); CHECK(no("{" BASE)); CHECK(no("")); CHECK(no("   "));
    CHECK(no("{'build': 1, 'note': 'x', 'urgent': false}")); CHECK(no("{" BASE ", \"a\": [1,]}")); CHECK(no("{" BASE ", \"a\": tru}"));
    CHECK(no("{" BASE ", \"a\": \"\\x\"}")); CHECK(no("{" BASE ", \"a\": \"a\nb\"}")); CHECK(no("{" BASE ", \"a\": -}")); CHECK(no("{" BASE ", \"a\": 1.}"));
    CHECK(ok("\r\n\t {" BASE "} \n"));
    { char s[600] = "{" BASE ", \"a\": "; for (int i = 0; i < 20; i++) strcat(s, "["); for (int i = 0; i < 20; i++) strcat(s, "]"); strcat(s, "}"); CHECK(no(s)); }   // 深度超限 | too deep
    { char s[600] = "{" BASE ", \"a\": "; for (int i = 0; i < 10; i++) strcat(s, "["); for (int i = 0; i < 10; i++) strcat(s, "]"); strcat(s, "}"); CHECK(ok(s)); }
    // 大小与 BOM | size and BOM
    { static char s[5000]; int n = snprintf(s, sizeof s, "{" BASE ", \"pad\": \""); while (n < 4094) s[n++] = 'x'; s[n++] = '"'; s[n++] = '}'; s[n] = 0;
      CHECK(n == 4096); CHECK(ok(s)); s[n - 2] = 'x'; s[n - 1] = '"'; s[n++] = '}'; s[n] = 0; CHECK(n == 4097); CHECK(no(s)); }
    CHECK(no("\xef\xbb\xbf{" BASE "}"));
    CHECK(dm_manifest_parse(NULL, 0, &m, why, sizeof why) == 0 && why[0]);
    // note 超长截断、带转义原样 | long note truncated, escapes kept raw
    { char s[700]; char note[400]; memset(note, 'n', 399); note[399] = 0; snprintf(s, sizeof s, "{\"build\": 7, \"note\": \"%s\", \"urgent\": true}", note);
      CHECK(dm_manifest_parse(s, strlen(s), &m, NULL, 0) == 1); CHECK(strlen(m.note) == sizeof m.note - 1); CHECK(m.build == 7 && m.urgent == 1); }
    printf("t_manifest: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
