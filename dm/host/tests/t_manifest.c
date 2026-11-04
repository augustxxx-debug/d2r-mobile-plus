// 远端更新清单严格解析（dm_manifest.c）回归：样例清单（与 fork 仓库 docs/latest.json 同字段）通过；必填缺失 / 类型错 / 取值越界 / 重复键 /
// 语法错（尾逗号、根后多余、非对象根、深度超限）/ 超长 / BOM / 社区字段转义都整份拒绝；未知键与嵌套同名键忽略；community 可省。
// Regression for strict manifest parsing (dm_manifest.c): the sample manifest (same fields as docs/latest.json in the fork) passes; missing required fields / wrong types /
// out-of-range values / duplicate keys / syntax errors (trailing comma, trailing content, non-object root, too deep) / oversize / BOM / escapes in community fields reject the
// whole manifest; unknown keys and nested same-name keys are ignored; community is optional.
#include "../dm_manifest.c"
#include "site_contract_cases.h"
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
    // community.site（服务端说明第 13 节）与线上清单的样子 | community.site (server notes section 13) and the live manifest's shape
    { const char *live = "{\"build\":1793254654,\"version\":\"1.0.0\",\"note\":\"1.0.0 RC1\",\"urgent\":false,\"community\":{\"qq_group\":\"1129913678\",\"github\":\"https://github.com/augustxxx-debug/d2r-mobile-plus\",\"site\":\"https://d2rplus.augustusplay.com/\"}}";
      CHECK(dm_manifest_parse(live, strlen(live), &m, why, sizeof why) == 1); CHECK(!strcmp(m.site, "https://d2rplus.augustusplay.com/")); CHECK(!strcmp(m.qq, "1129913678")); }
    // 合同样例（与 t_community.c 同一张表）；带 ? 的地址在 E-01 之前这里收、游戏内拒 | contract samples (the same table as t_community.c); before E-01 an address with ? passed here and failed in the game
    { char s[400], l[200];
      for (size_t i = 0; i < sizeof k_site_cases / sizeof *k_site_cases; i++) { snprintf(s, sizeof s, "{" BASE ", \"community\": {\"site\": \"%s\"}}", k_site_cases[i].site); CHECK(k_site_cases[i].ok ? ok(s) : no(s)); CHECK(dm_site_ok(k_site_cases[i].site) == k_site_cases[i].ok); }
      for (size_t i = 0; i < sizeof k_site_long / sizeof *k_site_long; i++) { site_contract_long(l, sizeof l, k_site_long[i].len); CHECK((int)strlen(l) == k_site_long[i].len); snprintf(s, sizeof s, "{" BASE ", \"community\": {\"site\": \"%s\"}}", l); CHECK(k_site_long[i].ok ? ok(s) : no(s)); } }
    CHECK(no("{" BASE ", \"community\": {\"site\": \"http://example.org/\"}}")); CHECK(no("{" BASE ", \"community\": {\"site\": \"https://\"}}"));
    CHECK(no("{" BASE ", \"community\": {\"site\": \"https://a b/\"}}")); CHECK(no("{" BASE ", \"community\": {\"site\": \"https:\\/\\/a\"}}"));
    CHECK(no("{" BASE ", \"community\": {\"site\": 1}}")); CHECK(no("{" BASE ", \"community\": {\"site\": \"https://a.example/\", \"site\": \"https://b.example/\"}}"));
    // community.mirror（国内镜像，gitee）：同 github 规则，前缀换 https://gitee.com/ | community.mirror (mainland mirror, gitee): the github rule with the gitee prefix
    CHECK(ok("{" BASE ", \"community\": {\"mirror\": \"https://gitee.com/augustxxx-debug/d2r-mobile-plus\"}}"));
    { const char *s = "{" BASE ", \"community\": {\"mirror\": \"https://gitee.com/augustxxx-debug/d2r-mobile-plus\"}}"; CHECK(dm_manifest_parse(s, strlen(s), &m, why, sizeof why) == 1 && !strcmp(m.mirror, "https://gitee.com/augustxxx-debug/d2r-mobile-plus")); }
    CHECK(no("{" BASE ", \"community\": {\"mirror\": \"https://github.com/a/b\"}}"));   // 必须是 gitee 前缀 | must carry the gitee prefix
    CHECK(no("{" BASE ", \"community\": {\"mirror\": \"http://gitee.com/a/b\"}}")); CHECK(no("{" BASE ", \"community\": {\"mirror\": \"https://gitee.com/\"}}"));
    CHECK(no("{" BASE ", \"community\": {\"mirror\": \"https://gitee.com.evil.example/a\"}}")); CHECK(no("{" BASE ", \"community\": {\"mirror\": \"https://gitee.com/a b\"}}"));
    CHECK(no("{" BASE ", \"community\": {\"mirror\": \"https://gitee.com/a\\/b\"}}"));   // 转义整份拒 | an escape rejects the whole manifest
    CHECK(no("{" BASE ", \"community\": {\"mirror\": \"https://gitee.com/a\", \"mirror\": \"https://gitee.com/b\"}}"));
    { char s[400]; snprintf(s, sizeof s, "{" BASE ", \"community\": {\"mirror\": \"https://gitee.com/%0143d\"}}", 0); CHECK(no(s)); }   // 18 + 143 = 161 字符 | 161 characters
    { char s[400]; snprintf(s, sizeof s, "{" BASE ", \"community\": {\"mirror\": \"https://gitee.com/%0142d\"}}", 0); CHECK(ok(s)); }   // 160 字符 | 160 characters
    // android.url：客户端不认这个键（不解析、不显示），任意内容都当未知键忽略，且不影响别的字段 | android.url is not a key we know (not parsed, not shown); any content is ignored as an unknown key and leaves other fields alone
    { const char *s = "{" BASE ", \"community\": {\"qq_group\": \"12345\", \"github\": \"https://github.com/a/b\", \"mirror\": \"https://gitee.com/a/b\"}, \"android\": {\"url\": \"https://d2rplus.augustusplay.com/d2r/x.apk\"}}";
      CHECK(dm_manifest_parse(s, strlen(s), &m, why, sizeof why) == 1 && !strcmp(m.qq, "12345") && !strcmp(m.github, "https://github.com/a/b") && !strcmp(m.mirror, "https://gitee.com/a/b") && m.build == 1793254654); }
    { const char *s = "{" BASE ", \"android\": {\"url\": \"https://evil.example/x.apk\"}}"; CHECK(dm_manifest_parse(s, strlen(s), &m, why, sizeof why) == 1 && !m.qq[0] && !m.github[0] && !m.site[0] && !m.mirror[0] && !strcmp(m.note, "1.0.0 RC1")); }
    CHECK(ok("{" BASE ", \"community\": {\"qq_group\": \"12345\", \"discord\": \"x\", \"min_build\": 3}, \"min_build\": 5}"));   // 未知键忽略 | unknown keys ignored
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
    // community.pans（网盘数组，规则见 dm_pan.h）：合法条目、第 5 条起丢弃、坏条只丢自己、pans 不是数组 / 元素不是对象 = 整份不合法（与 mirror 的严格口径一致）
    // community.pans (cloud drives, rules in dm_pan.h): valid entries, the 5th on dropped, a bad entry drops only itself, pans not an array / an element not an object = the whole manifest is invalid (matching mirror's strictness)
    { const char *s = "{" BASE ", \"community\": {\"pans\": ["
          "{\"name\": \"夸克\", \"icon\": \"quark\", \"url\": \"https://pan.quark.cn/s/abc\", \"code\": \"ab12\"},"
          "{\"name\": \"Baidu\", \"icon\": \"baidu\", \"url\": \"https://pan.baidu.com/s/1x\"},"
          "{\"name\": \"阿里\", \"icon\": \"aliyun\", \"url\": \"https://www.alipan.com/s/zz\"}]}}";
      CHECK(dm_manifest_parse(s, strlen(s), &m, why, sizeof why) == 1); CHECK(m.n_pans == 3);
      CHECK(!strcmp(m.pans[0].name, "夸克") && m.pans[0].icon == DM_PAN_QUARK && !strcmp(m.pans[0].url, "https://pan.quark.cn/s/abc") && !strcmp(m.pans[0].code, "ab12"));
      CHECK(!strcmp(m.pans[1].name, "Baidu") && m.pans[1].icon == DM_PAN_BAIDU && !m.pans[1].code[0]);
      CHECK(!strcmp(m.pans[2].name, "阿里") && m.pans[2].icon == DM_PAN_ALIYUN); }
    // 5 条截成前 4 条 | 5 entries are cut to the first 4
    { const char *s = "{" BASE ", \"community\": {\"pans\": ["
          "{\"name\": \"a\", \"icon\": \"quark\", \"url\": \"https://pan.quark.cn/s/a\"},"
          "{\"name\": \"b\", \"icon\": \"baidu\", \"url\": \"https://pan.baidu.com/s/b\"},"
          "{\"name\": \"c\", \"icon\": \"aliyun\", \"url\": \"https://www.alipan.com/s/c\"},"
          "{\"name\": \"d\", \"icon\": \"123pan\", \"url\": \"https://www.123pan.com/s/d\"},"
          "{\"name\": \"e\", \"icon\": \"quark\", \"url\": \"https://pan.quark.cn/s/e\"}]}}";
      CHECK(dm_manifest_parse(s, strlen(s), &m, why, sizeof why) == 1); CHECK(m.n_pans == 4);
      CHECK(!strcmp(m.pans[0].name, "a") && !strcmp(m.pans[3].name, "d")); }
    // 一条坏不影响其它条（坏链接 / 图标配错域名 / name 空 / name 13 个汉字 / 缺 icon）；code 不合规只丢 code | one bad entry leaves the others (bad url / wrong icon domain / empty name / 13-character name / missing icon); an invalid code is dropped alone
    { const char *s = "{" BASE ", \"community\": {\"pans\": ["
          "{\"name\": \"ok1\", \"icon\": \"quark\", \"url\": \"https://pan.quark.cn/s/a\", \"code\": \"abc\"},"
          "{\"name\": \"bad-url\", \"icon\": \"quark\", \"url\": \"https://pan.baidu.com/s/b\"},"
          "{\"name\": \"\", \"icon\": \"baidu\", \"url\": \"https://pan.baidu.com/s/c\"},"
          "{\"name\": \"ok2\", \"icon\": \"123pan\", \"url\": \"https://www.123pan.com/s/f\"},"
          "{\"name\": \"noicon\", \"url\": \"https://pan.baidu.com/s/e\"},"
          "{\"name\": \"中中中中中中中中中中中中中\", \"icon\": \"baidu\", \"url\": \"https://pan.baidu.com/s/d\"}]}}";
      CHECK(dm_manifest_parse(s, strlen(s), &m, why, sizeof why) == 1); CHECK(m.n_pans == 2);
      CHECK(!strcmp(m.pans[0].name, "ok1") && !m.pans[0].code[0] && !strcmp(m.pans[1].name, "ok2")); }
    { const char *s = "{" BASE ", \"community\": {\"pans\": [{\"name\": \"中中中中中中中中中中中中中\", \"icon\": \"baidu\", \"url\": \"https://pan.baidu.com/s/d\"}]}}";
      CHECK(dm_manifest_parse(s, strlen(s), &m, why, sizeof why) == 1 && m.n_pans == 0); }   // name 13 个汉字：该条作废 | 13-character name: entry invalid
    { const char *s = "{" BASE ", \"community\": {\"pans\": [{\"name\": \"noicon\", \"url\": \"https://pan.baidu.com/s/e\"}]}}";
      CHECK(dm_manifest_parse(s, strlen(s), &m, why, sizeof why) == 1 && m.n_pans == 0); }   // 缺 icon：该条作废 | missing icon: entry invalid
    // pans 不是数组 / 元素不是对象：整份清单不合法，why 写原因（与 mirror 一致） | pans not an array / element not an object: the whole manifest is invalid with a reason (same as mirror)
    CHECK(no("{" BASE ", \"community\": {\"pans\": {}}}"));
    CHECK(no("{" BASE ", \"community\": {\"pans\": \"x\"}}"));
    CHECK(no("{" BASE ", \"community\": {\"pans\": [\"x\"]}}"));
    CHECK(no("{" BASE ", \"community\": {\"pans\": [1]}}"));
    CHECK(no("{" BASE ", \"community\": {\"pans\": [], \"pans\": []}}"));
    { const char *s = "{" BASE ", \"community\": {\"pans\": {}}}"; char w[160]; CHECK(dm_manifest_parse(s, strlen(s), NULL, w, sizeof w) == 0 && strstr(w, "pans")); }
    // icon 是图片 URL：认不出 → generic，url 仍按四家白名单判（合法链接通过、别的域名丢这一条） | an image-URL icon: unknown -> generic, the url is still judged by the four-provider whitelist (a valid link passes, another host drops the entry)
    { const char *s = "{" BASE ", \"community\": {\"pans\": ["
          "{\"name\": \"pic\", \"icon\": \"https://img.example/q.png\", \"url\": \"https://pan.quark.cn/s/a\"}]}}";
      CHECK(dm_manifest_parse(s, strlen(s), &m, why, sizeof why) == 1); CHECK(m.n_pans == 1 && m.pans[0].icon == DM_PAN_GENERIC); }
    { const char *s = "{" BASE ", \"community\": {\"pans\": ["
          "{\"name\": \"pic\", \"icon\": \"https://img.example/q.png\", \"url\": \"https://evil.example/s/a\"}]}}";
      CHECK(dm_manifest_parse(s, strlen(s), &m, why, sizeof why) == 1); CHECK(m.n_pans == 0); }
    // 没有 pans 时结构原样（n_pans = 0，其余字段不受影响） | without pans the structure is as before (n_pans = 0, other fields unaffected)
    { const char *s = "{" BASE ", \"community\": {\"github\": \"https://github.com/a/b\"}}";
      CHECK(dm_manifest_parse(s, strlen(s), &m, why, sizeof why) == 1 && m.n_pans == 0 && !strcmp(m.github, "https://github.com/a/b")); }
    printf("t_manifest: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
