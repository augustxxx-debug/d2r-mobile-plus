// 取远端更新清单（第一期普通 latest.json，字段与校验见 dm/host/dm_manifest.h），给加载页底部的社区信息（官方免费渠道）用。
// 来源顺序（用户 2026-10-29 定）：出包时写进的地址按顺序试——自己的服务器在前、GitHub raw 在后；前一个超时 / 非 200（含跳转）/ 内容不合规才试下一个。
// 地址不在源码里：shell_apk.sh 读 DM_UPDATE_URLS（空格分隔，只收 https://）→ gradle -PdmUpdateUrls → BuildConfig.DM_UPDATE_URLS；没配 = 不联网。
// 一轮全失败隔 3 秒、6 秒再来，最多三轮；三轮都失败用缓存（最后一次合法的清单原文，files/community_manifest.json，先写临时文件再换名）；也没有缓存 = 不显示（不内置缺省值）。
// 合法但没有 community 字段 = 不显示，并照样写进缓存（清单明确说没有，旧值不留）。
// 只在首次导入 / 生成这段长等待里调（DmActivity），普通启动不联网。单次请求连接 10 秒、读 10 秒；不跟跳转；不用缓存；只读 4097 字节（多 1 个字节用来判超长）。
// User-Agent 只写「D2RMobilePlus-Android/<版本>」，不带系统给的机型串。
// 本类不依赖 android.*（桌面 JVM 上单测：tests/CommunityFetchTest.java）；校验、日志、HTTP 都由调用方注入。
// Fetch the remote update manifest (phase-1 plain latest.json, fields and validation in dm/host/dm_manifest.h) for the community info (official free channels) at the bottom of the loading page.
// Source order (user, 2026-10-29): the addresses written in at packaging time are tried in order - our own server first, GitHub raw after it; the next one is tried only when
// the previous one times out / is not 200 (redirects included) / has invalid content.
// The addresses are not in the source: shell_apk.sh reads DM_UPDATE_URLS (space separated, https:// only) → gradle -PdmUpdateUrls → BuildConfig.DM_UPDATE_URLS; none = no network access.
// When a whole round fails, retry after 3 s and 6 s, at most three rounds; after three failed rounds use the cache (the raw text of the last valid manifest,
// files/community_manifest.json, written to a temporary file then renamed); no cache either = nothing shown (no built-in default).
// Valid but without a community field = nothing shown, and it still goes into the cache (the manifest says there is none; no stale value is kept).
// Called only during the first import / generation wait (DmActivity); a normal start does not touch the network. Per request: 10 s connect, 10 s read; redirects not followed;
// no HTTP cache; at most 4097 bytes read (one extra byte to detect oversize). The User-Agent is only "D2RMobilePlus-Android/<version>", without the system's device model string.
// This class does not depend on android.* (unit tested on a desktop JVM: tests/CommunityFetchTest.java); validation, logging and HTTP are injected by the caller.
package com.xiaoboluo.d2r;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.List;

final class CommunityFetch {
    static final int MAX_BYTES = 4096, ROUNDS = 3, TIMEOUT_MS = 10000;

    interface Http { Response get(String url) throws IOException; }
    // 校验结果同 LoadPageView.manifest：{ "1" / "0", 原因, 群号, 链接, build } | validation result as LoadPageView.manifest: { "1" / "0", reason, group number, link, build }
    interface Validator { String[] check(byte[] body); }
    interface Log { void line(String s); }
    interface Sink { void community(String qq, String github, String from); }
    interface Sleeper { void sleep(long ms) throws InterruptedException; }
    static final class Response { final int code; final byte[] body; Response(int code, byte[] body) { this.code = code; this.body = body; } }

    // DM_UPDATE_URLS → 地址表：按空白 / 逗号分开，只收 https://，去重保序 | DM_UPDATE_URLS → address list: split on whitespace / commas, https:// only, deduplicated in order
    static List<String> urls(String config) {
        List<String> out = new ArrayList<>();
        if (config == null) return out;
        for (String u : config.trim().split("[\\s,]+")) if (u.startsWith("https://") && u.length() > 8 && !out.contains(u)) out.add(u);
        return out;
    }
    // 日志里只写主机名（地址路径不进日志；主机名是公开的） | the log carries only the host name (no path; host names are public)
    static String host(String url) { try { return new URL(url).getHost(); } catch (IOException e) { return "?"; } }

    private final List<String> sources; private final File cache; private final Http http; private final Validator validator; private final Log log; private final Sleeper sleeper;
    CommunityFetch(List<String> sources, File cache, Http http, Validator validator, Log log, Sleeper sleeper) {
        this.sources = sources; this.cache = cache; this.http = http; this.validator = validator; this.log = log; this.sleeper = sleeper;
    }

    // 跑一次（在后台线程）：交给 sink 的是第一份合法清单的社区两项（或缓存的）；都没有就不调 sink。返回来源说明（日志 / 单测用）
    // Run once (on a background thread): sink receives the community items of the first valid manifest (or the cached one); sink is not called when there is none.
    // Returns where it came from (for the log / unit tests)
    String run(Sink sink) throws InterruptedException {
        if (sources.isEmpty()) { log.line("社区信息：包里没有清单地址，不联网、不显示"); return "none"; }
        for (int round = 1; round <= ROUNDS; round++) {
            for (int i = 0; i < sources.size(); i++) {
                String url = sources.get(i), tag = "来源 " + (i + 1) + "/" + sources.size() + "（" + host(url) + "）第 " + round + " 轮";
                byte[] body;
                try {
                    Response r = http.get(url);
                    if (r.code != 200) { log.line("社区信息：" + tag + " HTTP " + r.code + "，换下一个"); continue; }
                    body = r.body;
                } catch (IOException e) { log.line("社区信息：" + tag + " 取不到（" + e.getClass().getSimpleName() + "），换下一个"); continue; }
                if (body.length > MAX_BYTES) { log.line("社区信息：" + tag + " 超过 " + MAX_BYTES + " 字节，换下一个"); continue; }
                String[] v = validator.check(body);
                if (!"1".equals(v[0])) { log.line("社区信息：" + tag + " 清单不合规（" + v[1] + "），换下一个"); continue; }
                writeCache(body);
                log.line("社区信息：" + tag + " 取到合法清单（build " + v[4] + "），群号" + (v[2].isEmpty() ? "无" : "有") + "、GitHub " + (v[3].isEmpty() ? "无" : "有"));
                deliver(sink, v, host(url));
                return "source" + (i + 1);
            }
            if (round < ROUNDS) sleeper.sleep(3000L * round);
        }
        byte[] cached = readCache();
        if (cached != null) {
            String[] v = validator.check(cached);
            if ("1".equals(v[0])) { log.line("社区信息：" + ROUNDS + " 轮都没取到，用上次的合法清单（缓存）"); deliver(sink, v, "cache"); return "cache"; }
            log.line("社区信息：缓存的清单不合规（" + v[1] + "），删掉"); cache.delete();
        }
        log.line("社区信息：" + ROUNDS + " 轮都没取到、也没有缓存，不显示");
        return "none";
    }
    private static void deliver(Sink sink, String[] v, String from) { if (!v[2].isEmpty() || !v[3].isEmpty()) sink.community(v[2], v[3], from); }

    private void writeCache(byte[] body) {
        if (cache == null) return;
        File tmp = new File(cache.getPath() + ".tmp");
        try { Files.write(tmp.toPath(), body); Files.move(tmp.toPath(), cache.toPath(), StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE); }
        catch (IOException e) { log.line("社区信息：缓存写不进去（" + e.getClass().getSimpleName() + "）"); tmp.delete(); }
    }
    private byte[] readCache() {
        if (cache == null || !cache.isFile() || cache.length() > MAX_BYTES) return null;
        try { return Files.readAllBytes(cache.toPath()); } catch (IOException e) { return null; }
    }

    // 真正的 HTTP：不跟跳转、不用缓存、连接 / 读各 10 秒、最多读 MAX_BYTES + 1 字节 | the real HTTP: no redirects, no cache, 10 s connect / read, at most MAX_BYTES + 1 bytes read
    static Http urlConnection(String userAgent) {
        return url -> {
            HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
            try {
                c.setInstanceFollowRedirects(false); c.setUseCaches(false); c.setConnectTimeout(TIMEOUT_MS); c.setReadTimeout(TIMEOUT_MS);
                c.setRequestProperty("User-Agent", userAgent); c.setRequestProperty("Cache-Control", "no-cache"); c.setRequestProperty("Accept", "application/json");
                int code = c.getResponseCode();
                if (code != 200) return new Response(code, new byte[0]);
                try (InputStream in = c.getInputStream()) {
                    ByteArrayOutputStream out = new ByteArrayOutputStream(); byte[] buf = new byte[1024]; int n;
                    while ((n = in.read(buf)) > 0 && out.size() <= MAX_BYTES) out.write(buf, 0, Math.min(n, MAX_BYTES + 1 - out.size()));
                    return new Response(code, out.toByteArray());
                }
            } finally { c.disconnect(); }
        };
    }
}
