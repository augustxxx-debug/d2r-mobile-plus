// community.site 的合同样例（dm_site.h）：t_manifest.c 与 t_community.c 用同一张表，保证加载页和游戏内的结论一样；60 / 61 / 160 / 161 字节由 site_contract_long 现拼。
// Contract samples of community.site (dm_site.h): t_manifest.c and t_community.c share this table so the loading page and the game agree; the 60 / 61 / 160 / 161 byte cases come from site_contract_long.
#pragma once
#include <stdio.h>
typedef struct { const char *site; int ok; } site_case;
static const site_case k_site_cases[] = {
    { "https://d2rplus.augustusplay.com/", 1 }, { "https://a.example", 1 }, { "https://a-b.example.com/d2r/v1/", 1 }, { "https://A1.Example.com/p_a-t.h~/(x)", 1 },
    { "https://example.com:443/", 0 }, { "https://example.com:443", 0 }, { "https://a.example/x:y", 0 },   // 端口与路径里的 : | a port, a : in the path
    { "https://a.example@evil.example/", 0 }, { "https://a.example/x@y", 0 },   // @
    { "https://a.example/x?y=1", 0 }, { "https://a.example?x", 0 }, { "https://a.example/#top", 0 }, { "https://#", 0 },   // ? #
    { "http://a.example/", 0 }, { "https://", 0 }, { "https:///x", 0 }, { "https://localhost/", 0 }, { "https://.a.example/", 0 }, { "https://a.example-/", 0 }, { "https://a b.example/", 0 } };
// 拼一个恰好 len 字节的合法形状地址 | build a well-shaped address of exactly len bytes
static inline const char *site_contract_long(char *b, size_t n, int len) { snprintf(b, n, "https://a.example/%0*d", len - 18, 0); return b; }
static const struct { int len, ok; } k_site_long[] = { { 60, 1 }, { 61, 0 }, { 160, 0 }, { 161, 0 } };
