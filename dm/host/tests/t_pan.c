// community.pans 规则头（dm_pan.h）回归：四家合法链接、http 拒绝、图标与域名不匹配拒绝、generic 只认四家并集、
// 认不出的图标回落 generic、code 4–8 位、name 按 UTF-8 字符数 1–12、域名提取。纯头文件，不需要其它模块。
// Regression for the community.pans rule header (dm_pan.h): the four providers, http rejected, icon / domain mismatch rejected,
// generic limited to the four providers, unknown icons fall back to generic, code 4-8, name counted in UTF-8 characters 1-12, host extraction.
#include "../dm_pan.h"
#include <stdio.h>
#include <string.h>
static int checks, fails;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

int main(void) {
    const char *quark = "https://pan.quark.cn/s/abc123", *baidu = "https://pan.baidu.com/s/1AbCd", *ali = "https://www.alipan.com/s/xYz", *ali2 = "https://www.aliyundrive.com/s/xYz", *pan123 = "https://www.123pan.com/s/abc";
    // 图标键：认识五个内置键，别的（含图片 URL / NULL）一律 generic | icon keys: the five built-ins, anything else (an image URL / NULL included) is generic
    CHECK(dm_pan_icon_of("quark") == DM_PAN_QUARK); CHECK(dm_pan_icon_of("baidu") == DM_PAN_BAIDU); CHECK(dm_pan_icon_of("aliyun") == DM_PAN_ALIYUN);
    CHECK(dm_pan_icon_of("123pan") == DM_PAN_123PAN); CHECK(dm_pan_icon_of("generic") == DM_PAN_GENERIC);
    CHECK(dm_pan_icon_of("weird") == DM_PAN_GENERIC); CHECK(dm_pan_icon_of("https://example.com/a.png") == DM_PAN_GENERIC); CHECK(dm_pan_icon_of(NULL) == DM_PAN_GENERIC);
    CHECK(!strcmp(dm_pan_icon_key(DM_PAN_QUARK), "quark") && !strcmp(dm_pan_icon_key(DM_PAN_BAIDU), "baidu") && !strcmp(dm_pan_icon_key(DM_PAN_ALIYUN), "aliyun"));
    CHECK(!strcmp(dm_pan_icon_key(DM_PAN_123PAN), "123pan") && !strcmp(dm_pan_icon_key(DM_PAN_GENERIC), "generic"));
    for (int k = 0; k <= DM_PAN_123PAN; k++) CHECK(dm_pan_icon_of(dm_pan_icon_key((dm_pan_icon)k)) == (dm_pan_icon)k);
    // 四家合法链接（图标配自己的前缀） | the four valid links with their own icons
    CHECK(dm_pan_url_ok(DM_PAN_QUARK, quark)); CHECK(dm_pan_url_ok(DM_PAN_BAIDU, baidu));
    CHECK(dm_pan_url_ok(DM_PAN_ALIYUN, ali)); CHECK(dm_pan_url_ok(DM_PAN_ALIYUN, ali2)); CHECK(dm_pan_url_ok(DM_PAN_123PAN, pan123));
    // http 拒绝；前缀必须带 /s/ 且后面还有内容 | http rejected; the prefix must include /s/ and be followed by something
    CHECK(!dm_pan_url_ok(DM_PAN_QUARK, "http://pan.quark.cn/s/abc")); CHECK(!dm_pan_url_ok(DM_PAN_GENERIC, "http://pan.quark.cn/s/abc"));
    CHECK(!dm_pan_url_ok(DM_PAN_GENERIC, "https://pan.quark.cn/s/")); CHECK(!dm_pan_url_ok(DM_PAN_GENERIC, "https://pan.quark.cn/x"));
    // 图标与域名不匹配 | icon / domain mismatch
    CHECK(!dm_pan_url_ok(DM_PAN_QUARK, baidu)); CHECK(!dm_pan_url_ok(DM_PAN_BAIDU, quark)); CHECK(!dm_pan_url_ok(DM_PAN_ALIYUN, pan123)); CHECK(!dm_pan_url_ok(DM_PAN_123PAN, ali));
    CHECK(!dm_pan_url_ok(DM_PAN_QUARK, quark + 4));   // 不是 https:// | not https://
    // generic：四家任一通过（含 aliyun 两个前缀）；别的域名 / 伪装前缀拒绝 | generic: any of the four (both aliyun prefixes); other hosts / fake prefixes rejected
    CHECK(dm_pan_url_ok(DM_PAN_GENERIC, quark)); CHECK(dm_pan_url_ok(DM_PAN_GENERIC, baidu)); CHECK(dm_pan_url_ok(DM_PAN_GENERIC, ali));
    CHECK(dm_pan_url_ok(DM_PAN_GENERIC, ali2)); CHECK(dm_pan_url_ok(DM_PAN_GENERIC, pan123));
    CHECK(!dm_pan_url_ok(DM_PAN_GENERIC, "https://evil.example/s/abc")); CHECK(!dm_pan_url_ok(DM_PAN_GENERIC, "https://pan.quark.cn.evil.example/s/abc"));
    CHECK(!dm_pan_url_ok(DM_PAN_GENERIC, "https://drive.google.com/s/abc")); CHECK(!dm_pan_url_ok(DM_PAN_GENERIC, ""));
    CHECK(!dm_pan_url_ok(DM_PAN_GENERIC, "https://pan.quark.cn/s/a b")); CHECK(!dm_pan_url_ok(DM_PAN_GENERIC, "https://pan.quark.cn/s/a\\b"));
    // 认不出的图标回落 generic，按 generic 的并集判 | an unknown icon falls back to generic and uses the union
    dm_pan_icon unk = dm_pan_icon_of("image-url"); CHECK(unk == DM_PAN_GENERIC && dm_pan_url_ok(unk, baidu));
    // 长度上限 160（同 github） | the 160-character cap (same as github)
    { char u[200]; snprintf(u, sizeof u, "https://pan.quark.cn/s/%0137d", 0); CHECK(strlen(u) == 160 && dm_pan_url_ok(DM_PAN_QUARK, u));
      snprintf(u, sizeof u, "https://pan.quark.cn/s/%0138d", 0); CHECK(strlen(u) == 161 && !dm_pan_url_ok(DM_PAN_QUARK, u)); }
    // code：4–8 位字母或数字 | code: 4-8 letters or digits
    CHECK(dm_pan_code_ok("abcd")); CHECK(dm_pan_code_ok("ABCD1234")); CHECK(dm_pan_code_ok("1234")); CHECK(dm_pan_code_ok("ab1d"));
    CHECK(!dm_pan_code_ok("abc")); CHECK(!dm_pan_code_ok("abcdefghi")); CHECK(!dm_pan_code_ok("ab-c")); CHECK(!dm_pan_code_ok("ab cd"));
    CHECK(!dm_pan_code_ok("abcd=")); CHECK(!dm_pan_code_ok("")); CHECK(!dm_pan_code_ok(NULL));
    { char c[16]; snprintf(c, sizeof c, "%07d", 0); CHECK(strlen(c) == 7 && dm_pan_code_ok(c)); snprintf(c, sizeof c, "%09d", 0); CHECK(strlen(c) == 9 && !dm_pan_code_ok(c)); }
    // name：按 UTF-8 字符数 1–12，不按字节 | name: 1-12 UTF-8 characters, not bytes
    CHECK(!dm_pan_name_ok("")); CHECK(!dm_pan_name_ok(NULL)); CHECK(dm_pan_name_ok("A")); CHECK(dm_pan_name_ok("abcdefghijkl")); CHECK(!dm_pan_name_ok("abcdefghijklm"));
    CHECK(dm_pan_name_ok("网盘")); CHECK(dm_pan_name_ok("中中中中中中中中中中中中")); CHECK(!dm_pan_name_ok("中中中中中中中中中中中中中"));
    CHECK(dm_pan_name_ok("Quark网盘 1"));   // 混合：9 个字符 | mixed: 9 characters
    CHECK(dm_pan_name_ok("😀😀😀😀😀😀😀😀😀😀😀😀")); CHECK(!dm_pan_name_ok("😀😀😀😀😀😀😀😀😀😀😀😀😀"));
    CHECK(!dm_pan_name_ok("abc\n")); CHECK(!dm_pan_name_ok("a\x80" "b")); CHECK(!dm_pan_name_ok("\xe4\xb8")); CHECK(!dm_pan_name_ok("a\xc0\xaf"));
    // 域名提取（确认框）：https:// 之后到 / : ? # 为止 | host extraction: after https:// up to / : ? #
    { char h[64];
      CHECK(dm_pan_host(quark, h, sizeof h) == 12 && !strcmp(h, "pan.quark.cn"));
      CHECK(dm_pan_host(ali, h, sizeof h) == 14 && !strcmp(h, "www.alipan.com"));
      CHECK(dm_pan_host(pan123, h, sizeof h) == 14 && !strcmp(h, "www.123pan.com"));
      CHECK(dm_pan_host("https://pan.baidu.com:443/s/x", h, sizeof h) == 13 && !strcmp(h, "pan.baidu.com"));
      CHECK(dm_pan_host("pan.quark.cn/s/x", h, sizeof h) == 0 && !h[0]);
      CHECK(dm_pan_host(NULL, h, sizeof h) == 0 && !h[0]);
      CHECK(dm_pan_host(quark, h, 8) == 7 && !strcmp(h, "pan.qua"));
      CHECK(dm_pan_host(quark, NULL, 0) == 12); }   // 只问长度 | length only
    // 打开前的最终判定 dm_pan_open_allowed：合规条目放行（具体网盘 / generic）；http、别家域名、图标与域名不符、空串、认不出的图标、NULL 全拒
    // Final check before opening, dm_pan_open_allowed: valid entries pass (concrete provider / generic); http, other hosts, icon / domain mismatch, empty, unknown icon and NULL are rejected
    { dm_pan_entry e; memset(&e, 0, sizeof e); snprintf(e.name, sizeof e.name, "q"); e.icon = DM_PAN_QUARK; snprintf(e.url, sizeof e.url, "%s", quark);
      CHECK(dm_pan_open_allowed(&e));
      e.icon = DM_PAN_GENERIC; CHECK(dm_pan_open_allowed(&e));
      snprintf(e.url, sizeof e.url, "http://pan.quark.cn/s/abc"); CHECK(!dm_pan_open_allowed(&e));
      e.icon = DM_PAN_QUARK; snprintf(e.url, sizeof e.url, "https://evil.example/s/abc"); CHECK(!dm_pan_open_allowed(&e));
      e.icon = DM_PAN_BAIDU; snprintf(e.url, sizeof e.url, "%s", quark); CHECK(!dm_pan_open_allowed(&e));
      e.icon = DM_PAN_QUARK; e.url[0] = 0; CHECK(!dm_pan_open_allowed(&e));
      e.icon = (dm_pan_icon)99; snprintf(e.url, sizeof e.url, "%s", quark); CHECK(!dm_pan_open_allowed(&e));
      CHECK(!dm_pan_open_allowed(NULL)); }
    printf("t_pan: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
