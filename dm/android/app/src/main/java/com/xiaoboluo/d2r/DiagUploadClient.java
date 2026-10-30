package com.xiaoboluo.d2r;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.ConnectException;
import java.net.HttpURLConnection;
import java.net.SocketTimeoutException;
import java.net.URL;
import java.net.UnknownHostException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.nio.file.attribute.PosixFilePermissions;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.security.SecureRandom;
import java.util.Base64;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import javax.net.ssl.SSLException;

// 自动上报崩溃的网络部分（iOS 版 dm/ios/Sources/dm_diag_upload.swift，协议 docs/COOP_PROTOCOL.md 第 7 节）。本类不依赖 android.*（桌面 JVM 上单测：tests/DiagUploadClientTest.java）。
// 次数与时机在公共宿主 dm/host/dm_crashreport.c；这里只做一次「尝试」：取挑战 → 必要时打包 → 签名 → 上传 → 归类，结果码与 dm_diagup.h 的 DM_DIAGUP_* 一致。
//   盘上报告同 iOS：<dir>/dm_crash_report.payload（已脱敏的载荷原文，重试要逐字节相同）、.id（upload_id、可能已送达标记），先写临时文件再换名。
//   自动的只有两件、各有上限：challenge_rejected 重取一次；413 缩小重打包最多 3 次（从盘上的载荷截头尾，日志文件这时可能已被新启动覆盖）。不跟跳转、不用缓存。
//   元数据正好六项；model 服务端只认苹果型号，安卓填 unknown（真实型号在日志头里）；os_version 必须带点（14 → 14.0）。私钥 32 字节种子存 no_backup 目录、0600，不进日志、不随备份走。
// Network part of automatic crash reports (iOS: dm/ios/Sources/dm_diag_upload.swift; protocol: docs/COOP_PROTOCOL.md section 7). No android.* here (unit-tested on the desktop JVM).
// Counts and timing live in the shared host dm/host/dm_crashreport.c; this class does one attempt: challenge → pack if needed → sign → upload → classify, with result codes as DM_DIAGUP_* in dm_diagup.h.
//   The report on disk as on iOS: <dir>/dm_crash_report.payload (the exact scrubbed payload; retries must be byte-identical), .id (upload_id, maybe-delivered flag); temp file, then rename.
//   Only two automatic bounded steps: one fresh challenge after challenge_rejected; up to 3 smaller re-packs after a 413 (trimming the stored payload, since the log files may have rotated). No redirects, no cache.
//   Exactly six metadata fields; the server's model rule only knows Apple models, so Android sends unknown (the real model is in the log head); os_version must contain a dot (14 → 14.0). The 32-byte private seed lives in the no-backup directory, 0600, never logged, not backed up.
final class DiagUploadClient {
    static final int OK = 1, UNCONFIRMED = 2, UNAVAILABLE = 3, MAINTENANCE = 4, RATE_LIMITED = 5, TOO_LARGE = 6, REJECTED = 7, CONFLICT = 8, PREPARE = 9, CONFIG = 10;
    static final int F_DUPLICATE = 1, F_MAYBE = 2;
    static final int PACK_LIMIT = 256 * 1024, HEAD_KEEP = 16 * 1024;
    private static final int MAX_REPLY = 16 * 1024, TIMEOUT_MS = 15000;

    interface Http { Response send(String method, String path, byte[] body) throws IOException; }
    interface Collect { String text() throws IOException; }   // 已脱敏全文（外壳的 C 部分 + scrubUpload） | the whole scrubbed text
    static final class Response { final int code; final byte[] body; final int retryAfter; Response(int code, byte[] body, int retryAfter) { this.code = code; this.body = body; this.retryAfter = retryAfter; } }
    static final class Result { final int outcome, flags, retryAfter; final String detail; Result(int outcome, String detail, int flags, int retryAfter) { this.outcome = outcome; this.detail = detail; this.flags = flags; this.retryAfter = retryAfter; } }
    static final class Report { final String id; final byte[] payload; boolean maybe; Report(String id, byte[] payload, boolean maybe) { this.id = id; this.payload = payload; this.maybe = maybe; } }

    // ---- 编码 | encoding ----
    static String b64(byte[] d) { return Base64.getUrlEncoder().withoutPadding().encodeToString(d); }
    static String hex(byte[] d) { StringBuilder s = new StringBuilder(); for (byte b : d) s.append(String.format("%02x", b)); return s.toString(); }
    private static byte[] sha256(byte[] d) { try { return MessageDigest.getInstance("SHA-256").digest(d); } catch (NoSuchAlgorithmException e) { throw new IllegalStateException(e); } }
    static byte[] transcript(String challenge, String id, byte[] payload) { return ("D2R-DIAGNOSTICS-V1\n" + challenge + "\n" + id + "\n" + hex(sha256(payload)) + "\n").getBytes(StandardCharsets.UTF_8); }
    static String jsonString(String s) {
        StringBuilder b = new StringBuilder("\"");
        for (int i = 0; i < s.length(); i++) { char c = s.charAt(i);
            if (c == '"' || c == '\\') b.append('\\').append(c); else if (c == '\n') b.append("\\n"); else if (c == '\r') b.append("\\r"); else if (c == '\t') b.append("\\t");
            else if (c < 0x20 || c == 0x7f) b.append('?');   // 服务端拒收别的控制字符（整份 invalid_log） | the server refuses other control characters
            else b.append(c); }
        return b.append('"').toString();
    }
    // 从扁平 JSON 对象里取字段（服务端的挑战 / 回执 / 错误都是扁平的）；取不到返回 null | a field from a flat JSON object
    static String field(byte[] json, String key) {
        Matcher m = Pattern.compile("\"" + Pattern.quote(key) + "\"\\s*:\\s*(\"([^\"\\\\]*)\"|-?[0-9]+|true|false)").matcher(new String(json, StandardCharsets.UTF_8));
        return m.find() ? (m.group(2) != null ? m.group(2) : m.group(1)) : null;
    }
    private static final String[] META = { "app_version", "build", "locale", "model", "os_version", "source_revision" };
    private static final String[] RULE = { "[A-Za-z0-9.+_-]{1,64}", "[A-Za-z0-9.+_-]{1,64}", "[A-Za-z0-9_-]{1,24}", "unknown", "[0-9]{1,3}(?:\\.[0-9]{1,3}){1,3}", "[0-9a-f]{7,40}|unknown" };
    private static final String[] FALLBACK = { "unknown", "unknown", "und", "unknown", "0.0", "unknown" };
    static byte[] payload(Map<String, String> metadata, String log) {
        StringBuilder b = new StringBuilder("{\"log\":").append(jsonString(log)).append(",\"metadata\":{");
        for (int i = 0; i < META.length; i++) { String v = metadata.getOrDefault(META[i], ""); if (!v.matches(RULE[i])) v = FALLBACK[i]; b.append(i == 0 ? "" : ",").append('"').append(META[i]).append("\":").append(jsonString(v)); }
        return b.append("},\"schema\":1}").toString().getBytes(StandardCharsets.UTF_8);
    }
    static String logOf(byte[] payload) {
        String s = new String(payload, StandardCharsets.UTF_8); int a = s.indexOf("\"log\":\""), e = s.indexOf("\",\"metadata\":", a); if (a != 0 && a != 1 || e < 0) return null;
        return s.substring(a + 7, e).replace("\\n", "\n").replace("\\r", "\r").replace("\\t", "\t").replace("\\\"", "\"").replace("\\\\", "\\");
    }
    // 留头（围栏、版本、设置摘要）和尾（崩溃前最后的日志）凑够 limit 字节 | keep the head (fence, version, settings) and the tail (the last log lines) within limit bytes
    static String trim(String text, int limit) {
        byte[] u = text.getBytes(StandardCharsets.UTF_8); if (u.length <= limit) return text;
        String mark = "\n\n===== （中间已省略 | middle omitted）=====\n\n"; int head = Math.min(HEAD_KEEP, limit / 2), tail = Math.max(0, limit - head - mark.getBytes(StandardCharsets.UTF_8).length);
        return new String(u, 0, head, StandardCharsets.UTF_8) + mark + new String(u, u.length - tail, tail, StandardCharsets.UTF_8);
    }
    // 上传专用的再一道（规则同 dm_diagnostic_scrub.h 的 dm_diagnostic_scrub_upload）：带令牌 / 票据 / 口令 / 私钥字样的整行、网址、IP；整份先经过 sharelog_android.c 的导出脱敏
    // the upload-only extra pass (rules as dm_diagnostic_scrub_upload): whole lines mentioning tokens / tickets / passwords / private keys, URLs, IP addresses; the whole text already went through the export scrub
    private static final Pattern SECRET_LINE = Pattern.compile("(?im)^.*(?:authorization|bearer\\s|cookie|private[ _-]?key|passw(?:or)?d|secret|(?:device|host|reader|operator|access|refresh)[ _-]?token|ticket\\s*[:=]|-----BEGIN).*$");
    static String scrubUpload(String t) {
        t = SECRET_LINE.matcher(t).replaceAll("<sensitive-line-redacted>");
        t = Pattern.compile("(?i)[a-z][a-z0-9+.-]*://[^\\s<>\"']+").matcher(t).replaceAll("<url>");
        t = Pattern.compile("(?<![0-9.])(?:[0-9]{1,3}\\.){3}[0-9]{1,3}(?![0-9.])").matcher(t).replaceAll("<ip>");
        return Pattern.compile("(?i)(?<![0-9A-Za-z:])(?:(?:[0-9a-f]{1,4}:){3,7}[0-9a-f]{1,4}|(?:[0-9a-f]{1,4}(?::[0-9a-f]{1,4})*)?::[0-9a-f]{1,4}(?::[0-9a-f]{1,4})*|[0-9a-f]{1,4}(?::[0-9a-f]{1,4})*::)(?![0-9A-Za-z:])").matcher(t).replaceAll("<ip>");
    }

    // ---- 盘上报告与私钥 | the report on disk and the private key ----
    private static void writeAtomic(File f, byte[] d) throws IOException { File t = new File(f.getPath() + ".tmp"); Files.write(t.toPath(), d); Files.move(t.toPath(), f.toPath(), StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE); }
    static Report load(File dir) {
        try { File p = new File(dir, "dm_crash_report.payload"), i = new File(dir, "dm_crash_report.id"); if (!p.isFile() || !i.isFile()) return null;
            String[] l = new String(Files.readAllBytes(i.toPath()), StandardCharsets.UTF_8).split("\n"); if (!l[0].matches("[0-9a-f]{32}")) return null;
            return new Report(l[0], Files.readAllBytes(p.toPath()), l.length > 1 && l[1].equals("maybe=1")); }
        catch (IOException e) { return null; }
    }
    static void save(File dir, Report r) { try { writeAtomic(new File(dir, "dm_crash_report.payload"), r.payload); writeAtomic(new File(dir, "dm_crash_report.id"), (r.id + "\nmaybe=" + (r.maybe ? 1 : 0) + "\n").getBytes(StandardCharsets.UTF_8)); } catch (IOException e) { /* 下次按没有报告处理 | treated as no report next time */ } }
    private static final SecureRandom RNG = new SecureRandom();
    static String newId() { byte[] r = new byte[16]; RNG.nextBytes(r); return hex(r); }
    static byte[] loadOrCreateSeed(File f) throws IOException {
        if (f.isFile()) { if (f.length() != 32 || Files.isSymbolicLink(f.toPath())) throw new IOException("key_file_unsafe"); return Files.readAllBytes(f.toPath()); }   // 格式不对就拒用，不覆盖 | anything else is refused, never overwritten
        byte[] seed = new byte[32]; RNG.nextBytes(seed); File t = new File(f.getPath() + ".tmp"); Files.deleteIfExists(t.toPath());
        Files.createFile(t.toPath(), PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rw-------"))); Files.write(t.toPath(), seed); Files.move(t.toPath(), f.toPath(), StandardCopyOption.ATOMIC_MOVE); return seed;
    }

    // 总开关暂停时只打包存盘，不联网 | while the server switch pauses uploads: pack to disk only, no network
    static void prepare(File dir, Map<String, String> metadata, Collect collect) throws IOException {
        if (load(dir) != null) return;
        String text = collect.text(); if (text == null) return;
        for (int cap = PACK_LIMIT; cap >= 2048; cap = cap * 3 / 4) { byte[] p = payload(metadata, trim(text, cap)); if (p.length <= PACK_LIMIT) { save(dir, new Report(newId(), p, false)); return; } }
    }
    // 按序拉清单，第一份 200 就用；都拉不到返回 null（调用方沿用缓存） | fetch the manifest in order, the first 200 wins; null when none (the caller keeps the cache)
    static byte[] fetchManifest(java.util.List<String> urls, CommunityFetch.Http http) {
        for (String u : urls) { try { CommunityFetch.Response r = http.get(u); if (r.code == 200 && r.body.length <= CommunityFetch.MAX_BYTES) return r.body; } catch (IOException e) { /* 换下一个 | next one */ } }
        return null;
    }

    // ---- 一次尝试 | one attempt ----
    private static String notSent(IOException e) {   // 请求肯定没到服务端的网络错误 → 原因码；其余算结果不明 | network errors where nothing reached the server → reason code; the rest count as indeterminate
        if (e instanceof UnknownHostException) return "dns"; if (e instanceof ConnectException) return "connect"; if (e instanceof SSLException) return "tls"; return null;
    }
    private static Result fail(int outcome, String detail, Report r, int retry) { boolean m = r != null && r.maybe; return new Result(m && outcome == UNAVAILABLE ? UNCONFIRMED : outcome, detail, m ? F_MAYBE : 0, retry); }
    private static String code(Response r, String dflt) { String c = field(r.body, "error"); return c != null && c.matches("[a-z0-9_]{1,64}") ? c : dflt; }
    private static int retryAfter(Response r) { int v = r.retryAfter; if (v <= 0) { String f = field(r.body, "retry_after"); if (f != null && f.matches("[0-9]{1,6}")) v = Integer.parseInt(f); } return v <= 0 ? 60 : Math.min(v, 3600); }
    static Result attempt(File dir, Http http, byte[] seed, Map<String, String> metadata, Collect collect) {
        Report report = load(dir); int budget = PACK_LIMIT, shrinks = 0; boolean refetched = false;
        try {
            while (true) {
                Response ch;
                try { ch = http.send("GET", "challenge", null); } catch (IOException e) { String why = notSent(e); return fail(UNAVAILABLE, why != null ? why : "network", report, 0); }
                if (ch.code != 200) { int c = ch.code; return c == 429 ? fail(RATE_LIMITED, code(ch, "rate_limited"), report, retryAfter(ch)) : c == 503 && "diagnostic_maintenance".equals(field(ch.body, "error")) ? fail(MAINTENANCE, "diagnostic_maintenance", report, retryAfter(ch)) : c >= 500 ? fail(UNAVAILABLE, code(ch, "http_" + c), report, retryAfter(ch)) : fail(REJECTED, code(ch, "http_" + c), report, 0); }
                String challenge = field(ch.body, "challenge"), maxS = field(ch.body, "max_payload_bytes");
                if (challenge == null || !challenge.matches("[A-Za-z0-9_-]{43}") || maxS == null || !maxS.matches("[0-9]{4,9}")) return fail(UNAVAILABLE, "bad_challenge", report, 0);
                int limit = Math.min(Integer.parseInt(maxS), budget);
                if (report == null || report.payload.length > limit) {
                    if (report != null && report.maybe) return fail(TOO_LARGE, "limit_shrank", report, 0);   // 上次可能已送达：不换 ID 另发一份 | maybe delivered: no second copy under a new ID
                    String text = report != null ? logOf(report.payload) : collect.text();
                    if (text == null) return fail(PREPARE, "collect", report, 0);
                    report = null;
                    for (int cap = limit; cap >= 2048; cap = cap * 3 / 4) { byte[] p = payload(metadata, trim(text, cap)); if (p.length <= limit) { report = new Report(newId(), p, false); break; } }
                    if (report == null) return fail(TOO_LARGE, "local_limit", null, 0);
                    save(dir, report);
                }
                byte[] sig = Ed25519Lite.sign(seed, transcript(challenge, report.id, report.payload));
                byte[] body = ("{\"challenge\":\"" + challenge + "\",\"payload\":\"" + b64(report.payload) + "\",\"public_key\":\"" + b64(Ed25519Lite.publicKey(seed)) + "\",\"signature\":\"" + b64(sig) + "\",\"upload_id\":\"" + report.id + "\"}").getBytes(StandardCharsets.UTF_8);
                Response r;
                try { r = http.send("POST", "reports", body); }
                catch (IOException e) { String why = notSent(e); if (why != null) return fail(UNAVAILABLE, why, report, 0); report.maybe = true; save(dir, report); return fail(UNCONFIRMED, e instanceof SocketTimeoutException ? "timeout" : "response_lost", report, 0); }
                if (r.code == 200 || r.code == 201) {
                    String id = field(r.body, "report_id"), dup = field(r.body, "duplicate");
                    if (id == null || !id.matches("[0-9a-f]{32}") || dup == null) { report.maybe = true; save(dir, report); return fail(UNCONFIRMED, "bad_receipt", report, 0); }
                    return new Result(OK, id, "true".equals(dup) ? F_DUPLICATE : 0, 0);
                }
                String c = code(r, "http_" + r.code);
                if (r.code == 403 && c.equals("challenge_rejected") && !refetched) { refetched = true; continue; }
                if (r.code == 413) {
                    if (shrinks < 3 && !report.maybe && report.payload.length / 2 >= 4096) { shrinks++; budget = report.payload.length / 2; continue; }   // 服务端说太大：一定没存 | the server said too large: certainly not stored
                    return fail(TOO_LARGE, c, report, 0);
                }
                if (r.code == 409 && c.equals("upload_conflict")) return fail(CONFLICT, c, report, 0);
                if (r.code == 429) return fail(RATE_LIMITED, c, report, retryAfter(r));
                if (r.code == 503 && c.equals("diagnostic_maintenance")) return fail(MAINTENANCE, c, report, retryAfter(r));
                if (r.code == 503 && field(r.body, "error") != null) return fail(UNAVAILABLE, c, report, retryAfter(r));
                if (r.code >= 500) { report.maybe = true; save(dir, report); return fail(UNCONFIRMED, c, report, 0); }   // 网关 / 500：请求可能已处理 | gateway / 500: the request may have been processed
                return fail(REJECTED, c, report, 0);
            }
        } catch (IOException e) { return fail(PREPARE, "collect", report, 0); }
    }

    // 真正的 HTTP：只许 https（调试包可 http://127.0.0.1）、不带路径 / 账号 / 查询；不跟跳转、不用缓存；回复最多读 16 KiB | the real HTTP: https only (debug may use loopback), no path / credentials / query; no redirects, no cache; replies capped at 16 KiB
    static boolean validOrigin(String o, boolean allowLoopback) { return o != null && (o.matches("https://[A-Za-z0-9.-]+(:[0-9]{1,5})?/?") || (allowLoopback && o.matches("http://127\\.0\\.0\\.1(:[0-9]{1,5})?/?"))); }
    static Http urlConnection(String origin, String userAgent) {
        String base = (origin.endsWith("/") ? origin : origin + "/") + "diagnostics/v1/";
        return (method, path, body) -> {
            HttpURLConnection c = (HttpURLConnection) new URL(base + path).openConnection();
            try {
                c.setInstanceFollowRedirects(false); c.setUseCaches(false); c.setConnectTimeout(TIMEOUT_MS); c.setReadTimeout(TIMEOUT_MS); c.setRequestMethod(method);
                c.setRequestProperty("User-Agent", userAgent); c.setRequestProperty("Accept", "application/json");
                if (body != null) { c.setDoOutput(true); c.setRequestProperty("Content-Type", "application/json"); c.setFixedLengthStreamingMode(body.length); try (OutputStream o = c.getOutputStream()) { o.write(body); } }
                int code = c.getResponseCode(); InputStream in = code < 400 ? c.getInputStream() : c.getErrorStream(); ByteArrayOutputStream out = new ByteArrayOutputStream();
                if (in != null) try (InputStream i = in) { byte[] buf = new byte[1024]; int n; while ((n = i.read(buf)) > 0 && out.size() < MAX_REPLY) out.write(buf, 0, Math.min(n, MAX_REPLY - out.size())); }
                int ra = 0; String h = c.getHeaderField("Retry-After"); if (h != null && h.trim().matches("[0-9]{1,6}")) ra = Integer.parseInt(h.trim());
                return new Response(code, out.toByteArray(), ra);
            } finally { c.disconnect(); }
        };
    }
    static Map<String, String> metadata(String appVersion, String build, String label, String osRelease, String locale) {
        Map<String, String> m = new LinkedHashMap<>(); m.put("app_version", appVersion); m.put("build", build); m.put("model", "unknown"); m.put("locale", locale);
        m.put("os_version", osRelease != null && osRelease.matches("[0-9]{1,3}") ? osRelease + ".0" : osRelease);
        Matcher h = Pattern.compile("\\b[0-9a-f]{7,40}\\b").matcher(label == null ? "" : label); m.put("source_revision", h.find() ? h.group() : "unknown");
        return m;
    }
}
