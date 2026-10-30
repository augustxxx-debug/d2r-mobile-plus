package com.xiaoboluo.d2r;

import java.io.File;
import java.io.IOException;
import java.net.SocketTimeoutException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

// 社区信息的双来源拉取（CommunityFetch）：地址表解析；服务器优先、超时 / 非 200 / 跳转 / 超长 / 不合规才换 GitHub raw；只配 raw 一个也能用；三轮重试与间隔；
// 合法清单写缓存、全失败用缓存、坏缓存删掉；没有 community = 不显示但写缓存；没配地址 = 不联网。
// Two-source fetch of the community info (CommunityFetch): address list parsing; server first, GitHub raw only on timeout / non-200 / redirect / oversize / invalid content;
// works with raw alone; three rounds with delays; a valid manifest goes to the cache, the cache is used when everything fails, a bad cache is deleted;
// no community = nothing shown but cached; no addresses = no network access.
public class CommunityFetchTest {
    private static int cases;
    private static void equal(Object want, Object actual) { cases++; if (!want.equals(actual)) throw new AssertionError("want=" + want + " actual=" + actual); }
    static final String SERVER = "https://example.org/d2r/v1/latest.json", RAW = "https://raw.example.com/o/r/main/docs/latest.json";
    static final String GOOD = "{\"build\": 5, \"note\": \"n\", \"urgent\": false, \"community\": {\"qq_group\": \"1129913678\", \"github\": \"https://github.com/a/b\"}}";
    static final String GOOD_GH = "{\"build\": 6, \"note\": \"n\", \"urgent\": false, \"community\": {\"github\": \"https://github.com/c/d\"}}";
    static final String NO_COMM = "{\"build\": 7, \"note\": \"n\", \"urgent\": false}";
    static final String SITE_ONLY = "{\"build\": 8, \"note\": \"n\", \"urgent\": false, \"community\": {\"site\": \"https://s.example/\"}}";

    // 测试用的校验：只认上面几份（真机走原生 dm_manifest_parse，单测在 dm/host/tests/t_manifest.c） | test validator: knows only the samples above (the device uses native dm_manifest_parse, unit tested in t_manifest.c)
    static String[] check(byte[] b) {
        String s = new String(b, StandardCharsets.UTF_8);
        if (s.equals(GOOD)) return new String[]{"1", "", "1129913678", "https://github.com/a/b", "5"};
        if (s.equals(GOOD_GH)) return new String[]{"1", "", "", "https://github.com/c/d", "6"};
        if (s.equals(NO_COMM)) return new String[]{"1", "", "", "", "7", ""};
        if (s.equals(SITE_ONLY)) return new String[]{"1", "", "", "", "8", "https://s.example/"};
        return new String[]{"0", "坏", "", "", ""};
    }
    static final class Fake implements CommunityFetch.Http {
        final List<String> calls = new ArrayList<>(); final java.util.Map<String, Object[]> plan = new java.util.HashMap<>();   // url → 依次返回的结果（int 码 / String 内容 / IOException） | url → results in turn
        void on(String url, Object... results) { plan.put(url, results); }
        public CommunityFetch.Response get(String url) throws IOException {
            int n = 0; for (String c : calls) if (c.equals(url)) n++; calls.add(url);
            Object[] p = plan.get(url); Object r = p == null ? (Object) new IOException("no route") : p[Math.min(n, p.length - 1)];
            if (r instanceof IOException) throw (IOException) r;
            if (r instanceof Integer) return new CommunityFetch.Response((Integer) r, new byte[0]);
            return new CommunityFetch.Response(200, ((String) r).getBytes(StandardCharsets.UTF_8));
        }
    }
    static final List<String> got = new ArrayList<>(), logs = new ArrayList<>(), sleeps = new ArrayList<>();
    static String run(List<String> urls, File cache, Fake http) throws Exception {
        got.clear(); logs.clear(); sleeps.clear();
        return new CommunityFetch(urls, cache, http, CommunityFetchTest::check, logs::add, ms -> sleeps.add(String.valueOf(ms))).run((q, g, w, from) -> got.add(q + "|" + g + (w.isEmpty() ? "" : "|" + w) + "|" + from));
    }

    public static void main(String[] args) throws Exception {
        // 地址表：只收 https、去重保序、空白 / 逗号分开 | address list: https only, deduplicated in order, split on whitespace / commas
        equal(Arrays.asList(SERVER, RAW), CommunityFetch.urls(" " + SERVER + "\n" + RAW + "," + SERVER + " http://plain.example/x ftp://x https:// "));
        equal(0, CommunityFetch.urls("").size()); equal(0, CommunityFetch.urls(null).size());
        equal("example.org", CommunityFetch.host(SERVER));
        File dir = Files.createTempDirectory("cft").toFile(); File cache = new File(dir, "community_manifest.json");
        List<String> both = Arrays.asList(SERVER, RAW), rawOnly = Arrays.asList(RAW);

        // 1. 服务器好：只问服务器一次，取它的值，写缓存 | server fine: asked once, its values used, cached
        Fake f = new Fake(); f.on(SERVER, GOOD); f.on(RAW, GOOD_GH);
        equal("source1", run(both, cache, f)); equal(Arrays.asList(SERVER), f.calls); equal(Arrays.asList("1129913678|https://github.com/a/b|example.org"), got);
        equal(GOOD, new String(Files.readAllBytes(cache.toPath()), StandardCharsets.UTF_8)); equal(false, new File(cache.getPath() + ".tmp").exists());
        // 2. 服务器 526 / 超时 / 跳转 / 超长 / 不合规 → 换 raw | server 526 / timeout / redirect / oversize / invalid → raw
        Object[] badServer = { 526, new SocketTimeoutException("t"), 301, new String(new char[4097]).replace('\0', 'x'), "{\"bad\": 1}" };
        for (Object b : badServer) { f = new Fake(); f.on(SERVER, b); f.on(RAW, GOOD_GH);
            equal("source2", run(both, cache, f)); equal(Arrays.asList(SERVER, RAW), f.calls); equal(Arrays.asList("|https://github.com/c/d|raw.example.com"), got); equal(0, sleeps.size()); }
        // 3. 只配 raw：照样能用 | raw only: works
        f = new Fake(); f.on(RAW, GOOD); equal("source1", run(rawOnly, cache, f)); equal(Arrays.asList(RAW), f.calls); equal(1, got.size());
        // 4. 第一轮两个都失败、第二轮服务器好了：隔 3 秒重试，取服务器的 | both fail in round 1, server recovers in round 2: retry after 3 s, server's values
        f = new Fake(); f.on(SERVER, 503, GOOD); f.on(RAW, new IOException("dns"));
        equal("source1", run(both, cache, f)); equal(Arrays.asList(SERVER, RAW, SERVER), f.calls); equal(Arrays.asList("3000"), sleeps); equal("1129913678|https://github.com/a/b|example.org", got.get(0));
        // 5. 三轮都失败 → 用缓存（上一份合法的），间隔 3、6 秒，每个来源各问三次 | three failed rounds → cache (the last valid one), delays 3 and 6 s, each source asked three times
        f = new Fake(); f.on(SERVER, 526); f.on(RAW, 404);
        equal("cache", run(both, cache, f)); equal(6, f.calls.size()); equal(Arrays.asList("3000", "6000"), sleeps); equal(Arrays.asList("1129913678|https://github.com/a/b|cache"), got);
        // 6. 坏缓存：删掉、不显示 | bad cache: deleted, nothing shown
        Files.write(cache.toPath(), "{oops".getBytes(StandardCharsets.UTF_8));
        f = new Fake(); equal("none", run(both, cache, f)); equal(0, got.size()); equal(false, cache.exists());
        // 7. 没缓存、全失败：不显示（不内置缺省值） | no cache, all fail: nothing shown (no built-in default)
        f = new Fake(); equal("none", run(both, cache, f)); equal(0, got.size()); equal(true, logs.get(logs.size() - 1).contains("不显示"));
        // 8. 合法但没有 community：不显示，但写缓存（之后全失败也不会用到旧值） | valid without community: nothing shown, but cached (an old value is never used later)
        Files.write(cache.toPath(), GOOD.getBytes(StandardCharsets.UTF_8));
        f = new Fake(); f.on(SERVER, NO_COMM); equal("source1", run(both, cache, f)); equal(0, got.size()); equal(NO_COMM, new String(Files.readAllBytes(cache.toPath()), StandardCharsets.UTF_8));
        f = new Fake(); equal("cache", run(both, cache, f)); equal(0, got.size());
        // 8b. 只有官网也交给页脚 | a site alone reaches the footer too
        f = new Fake(); f.on(SERVER, SITE_ONLY); equal("source1", run(both, cache, f)); equal(Arrays.asList("||https://s.example/|example.org"), got);
        // 9. 没配地址：不联网 | no addresses: no network access
        f = new Fake(); equal("none", run(new ArrayList<>(), cache, f)); equal(0, f.calls.size()); equal(0, sleeps.size());
        // 10. 日志不带地址路径（只有主机名） | the log carries no address path (host name only)
        f = new Fake(); f.on(SERVER, 526); f.on(RAW, GOOD); run(both, cache, f);
        for (String l : logs) { equal(false, l.contains("/d2r/v1/")); equal(false, l.contains("/docs/latest.json")); }
        System.out.println("CommunityFetchTest: " + cases + " 例全过");
    }
}
