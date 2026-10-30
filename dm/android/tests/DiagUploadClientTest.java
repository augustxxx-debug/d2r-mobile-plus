package com.xiaoboluo.d2r;

import java.io.File;
import java.io.IOException;
import java.net.SocketTimeoutException;
import java.net.UnknownHostException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

// 自动上报崩溃的协议与盘上报告（DiagUploadClient / Ed25519Lite）：RFC 8032 向量；请求体正好五个字段、载荷三个字段与六项元数据、签名内容逐字节（与 iOS 版同一公式）；丢响应后同 ID 同载荷；413 从盘上载荷缩小、不再收集；4xx / 5xx / 429 / 维护 / 离线 的归类；上传专用脱敏。不连网。
// Protocol and on-disk report of automatic crash reports: RFC 8032 vectors; exactly five body fields, three payload fields and six metadata fields, byte-exact signed text; same id and payload after a lost response; a 413 shrinks from the stored payload without collecting again; classification of 4xx / 5xx / 429 / maintenance / offline; upload scrub. No network.
public class DiagUploadClientTest {
    private static int cases;
    private static void check(boolean ok, String what) { cases++; if (!ok) throw new AssertionError(what); }
    private static byte[] unhex(String s) { byte[] o = new byte[s.length() / 2]; for (int i = 0; i < o.length; i++) o[i] = (byte) Integer.parseInt(s.substring(2 * i, 2 * i + 2), 16); return o; }
    interface Answer { DiagUploadClient.Response on(String method, String path, byte[] body) throws IOException; }
    static final class Fake implements DiagUploadClient.Http {
        final ArrayDeque<Answer> script = new ArrayDeque<>(); final List<byte[]> posts = new ArrayList<>(); int requests;
        @Override public DiagUploadClient.Response send(String m, String p, byte[] b) throws IOException { requests++; if (b != null) posts.add(b); Answer a = script.poll(); if (a == null) throw new IOException("no script"); return a.on(m, p, b); }
    }
    static final String CHALLENGE = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    static Answer challenge() { return (m, p, b) -> { check(m.equals("GET") && p.equals("challenge"), "challenge request"); return new DiagUploadClient.Response(200, ("{\"challenge\":\"" + CHALLENGE + "\",\"expires_in\":120,\"max_payload_bytes\":614400,\"retention_days\":7}").getBytes(StandardCharsets.UTF_8), 0); }; }
    static Answer reply(int code, String json, int retry) { return (m, p, b) -> new DiagUploadClient.Response(code, json.getBytes(StandardCharsets.UTF_8), retry); }
    static Answer receipt(boolean dup) { return reply(201, "{\"report_id\":\"0123456789abcdef0123456789abcdef\",\"expires_at\":1893456000,\"duplicate\":" + dup + "}", 0); }
    static String f(byte[] json, String k) { return DiagUploadClient.field(json, k); }
    static File newDir() throws IOException { return Files.createTempDirectory("dmcr").toFile(); }
    static void wipe(File d) { File[] l = d.listFiles(); if (l != null) for (File x : l) x.delete(); }
    public static void main(String[] a) throws Exception {
        // RFC 8032 7.1 向量 1 / 2 | RFC 8032 section 7.1 vectors 1 and 2
        byte[] s1 = unhex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
        check(DiagUploadClient.hex(Ed25519Lite.publicKey(s1)).equals("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"), "vector 1 public key");
        check(DiagUploadClient.hex(Ed25519Lite.sign(s1, new byte[0])).equals("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"), "vector 1 signature");
        byte[] s2 = unhex("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb");
        check(DiagUploadClient.hex(Ed25519Lite.sign(s2, new byte[] { 0x72 })).equals("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"), "vector 2 signature");
        byte[] seed = new byte[32]; seed[0] = 7; File dir = newDir();
        var meta = DiagUploadClient.metadata("1.0.2", "120", "v1.0.2+5 · abc1234", "14", "zh-CN");
        check(meta.get("os_version").equals("14.0") && meta.get("source_revision").equals("abc1234") && meta.get("model").equals("unknown"), "metadata normalised");
        // 成功路径：请求体五个字段，载荷三个字段 + 六项元数据，签名内容 | success path
        Fake h = new Fake(); h.script.add(challenge()); h.script.add(receipt(false)); int[] collects = { 0 };
        DiagUploadClient.Collect c = () -> { collects[0]++; return "fence cause=crash\n日志\t\"quoted\"\\back\nlast"; };
        DiagUploadClient.Result r = DiagUploadClient.attempt(dir, h, seed, meta, c);
        check(r.outcome == DiagUploadClient.OK && r.detail.equals("0123456789abcdef0123456789abcdef"), "ok"); check(collects[0] == 1, "collected once");
        byte[] body = h.posts.get(0); String bs = new String(body, StandardCharsets.UTF_8);
        for (String k : new String[] { "challenge", "payload", "public_key", "signature", "upload_id" }) check(f(body, k) != null, "field " + k);
        check(bs.chars().filter(x -> x == ':').count() == 5, "exactly five fields");
        byte[] payload = java.util.Base64.getUrlDecoder().decode(f(body, "payload")); String ps = new String(payload, StandardCharsets.UTF_8);
        check(ps.startsWith("{\"log\":\"") && ps.endsWith("},\"schema\":1}"), "payload shape " + ps);
        for (String k : new String[] { "app_version", "build", "source_revision", "model", "os_version", "locale" }) check(ps.contains("\"" + k + "\":"), "metadata " + k);
        check(DiagUploadClient.logOf(payload).equals("fence cause=crash\n日志\t\"quoted\"\\back\nlast"), "log round trip");
        check(new String(DiagUploadClient.transcript(CHALLENGE, f(body, "upload_id"), payload), StandardCharsets.UTF_8).startsWith("D2R-DIAGNOSTICS-V1\n" + CHALLENGE + "\n" + f(body, "upload_id") + "\n"), "transcript");
        check(f(body, "signature").equals(DiagUploadClient.b64(Ed25519Lite.sign(seed, DiagUploadClient.transcript(CHALLENGE, f(body, "upload_id"), payload)))), "signature over the exact transcript");
        // 丢响应：盘上留着、标可能已送达；下次同 ID 同载荷，不再收集 | lost response: kept, flagged; next time same id and payload, no collection
        wipe(dir); h = new Fake(); h.script.add(challenge()); h.script.add((m, p, b) -> { throw new SocketTimeoutException("t"); }); collects[0] = 0;
        r = DiagUploadClient.attempt(dir, h, seed, meta, c); check(r.outcome == DiagUploadClient.UNCONFIRMED && r.detail.equals("timeout") && (r.flags & DiagUploadClient.F_MAYBE) != 0, "unconfirmed");
        DiagUploadClient.Report saved = DiagUploadClient.load(dir); check(saved != null && saved.maybe, "saved maybe");
        h = new Fake(); h.script.add(challenge()); h.script.add(receipt(true)); r = DiagUploadClient.attempt(dir, h, seed, meta, () -> { throw new AssertionError("must not collect"); });
        check(r.outcome == DiagUploadClient.OK && (r.flags & DiagUploadClient.F_DUPLICATE) != 0, "duplicate ok"); check(f(h.posts.get(0), "upload_id").equals(saved.id) && java.util.Arrays.equals(java.util.Base64.getUrlDecoder().decode(f(h.posts.get(0), "payload")), saved.payload), "same id and bytes");
        // 413：从盘上载荷缩小（留头尾）、新 ID、不再收集 | 413: shrink from the stored payload, keep head and tail, new id
        wipe(dir); String big = "HEAD-MARK\n" + "x".repeat(100000) + "\nTAIL-MARK"; h = new Fake(); h.script.add(challenge()); h.script.add(reply(413, "{\"error\":\"payload_too_large\"}", 0)); h.script.add(challenge()); h.script.add(receipt(false)); collects[0] = 0;
        r = DiagUploadClient.attempt(dir, h, seed, meta, () -> { collects[0]++; return big; });
        check(r.outcome == DiagUploadClient.OK && collects[0] == 1, "shrunk"); byte[] p2 = java.util.Base64.getUrlDecoder().decode(f(h.posts.get(1), "payload")); String l2 = DiagUploadClient.logOf(p2);
        check(p2.length <= 60000 && l2.startsWith("HEAD-MARK") && l2.endsWith("TAIL-MARK"), "head and tail kept"); check(!f(h.posts.get(0), "upload_id").equals(f(h.posts.get(1), "upload_id")), "new id after 413");
        // 归类 | classification
        Object[][] table = { { 429, "rate_limited", 120, DiagUploadClient.RATE_LIMITED }, { 503, "diagnostic_maintenance", 90, DiagUploadClient.MAINTENANCE }, { 400, "invalid_metadata", 0, DiagUploadClient.REJECTED }, { 409, "upload_conflict", 0, DiagUploadClient.CONFLICT }, { 500, "request_failed", 0, DiagUploadClient.UNCONFIRMED }, { 503, "storage_capacity", 60, DiagUploadClient.UNAVAILABLE } };
        for (Object[] t : table) { wipe(dir); h = new Fake(); h.script.add(challenge()); h.script.add(reply((Integer) t[0], "{\"error\":\"" + t[1] + "\"}", (Integer) t[2])); r = DiagUploadClient.attempt(dir, h, seed, meta, c); check(r.outcome == (Integer) t[3] && r.detail.equals(t[1]), "class " + t[0] + " got " + r.outcome); if ((Integer) t[2] > 0) check(r.retryAfter == (Integer) t[2], "retry after " + t[0]); }
        wipe(dir); h = new Fake(); h.script.add((m, p, b) -> { throw new UnknownHostException("x"); }); r = DiagUploadClient.attempt(dir, h, seed, meta, c); check(r.outcome == DiagUploadClient.UNAVAILABLE && r.detail.equals("dns"), "dns"); check(h.posts.isEmpty(), "nothing posted");
        h = new Fake(); h.script.add(reply(503, "{\"error\":\"diagnostic_maintenance\"}", 30)); r = DiagUploadClient.attempt(dir, h, seed, meta, c); check(r.outcome == DiagUploadClient.MAINTENANCE && r.retryAfter == 30, "challenge maintenance");
        // 地址规则：只许 https 根地址；回环只给调试包 | origin rules
        check(DiagUploadClient.validOrigin("https://diag.example.invalid", false) && !DiagUploadClient.validOrigin("http://diag.example.invalid", true) && !DiagUploadClient.validOrigin("https://diag.example.invalid/x", false) && !DiagUploadClient.validOrigin("", true) && !DiagUploadClient.validOrigin(null, true) && !DiagUploadClient.validOrigin("http://127.0.0.1:9", false) && DiagUploadClient.validOrigin("http://127.0.0.1:9", true), "origin rules");
        // 总开关暂停：只打包、零请求；恢复后发同一份；清单按序拉、拉不到返回 null | server switch paused: pack only, zero requests; the same report on resume; manifests in order, null when none
        wipe(dir); int[] packs = { 0 }; DiagUploadClient.prepare(dir, meta, () -> { packs[0]++; return "packed while paused"; }); DiagUploadClient.prepare(dir, meta, () -> { packs[0]++; return "again"; });
        DiagUploadClient.Report held = DiagUploadClient.load(dir); check(packs[0] == 1 && held != null, "packed once");
        h = new Fake(); h.script.add(challenge()); h.script.add(receipt(false)); r = DiagUploadClient.attempt(dir, h, seed, meta, () -> { throw new AssertionError("must not collect"); });
        check(r.outcome == DiagUploadClient.OK && f(h.posts.get(0), "upload_id").equals(held.id), "resume sends the packed report");
        List<String> got = new ArrayList<>();
        byte[] mf = DiagUploadClient.fetchManifest(List.of("https://a.invalid/m", "https://b.invalid/m"), u -> { got.add(u); if (u.contains("a.invalid")) throw new IOException("x"); return new CommunityFetch.Response(200, "{}".getBytes(StandardCharsets.UTF_8)); });
        check(mf != null && got.size() == 2, "second source used");
        check(DiagUploadClient.fetchManifest(List.of("https://a.invalid/m"), u -> new CommunityFetch.Response(404, new byte[0])) == null, "404 = not fetched"); check(DiagUploadClient.fetchManifest(new ArrayList<>(), u -> { throw new AssertionError(); }) == null, "no addresses = no request");
        // 上传脱敏 | upload scrub
        String sc = DiagUploadClient.scrubUpload("大厅 https://lobby.example.invalid/x 连 203.0.113.7 与 fe80:0:0:0:1:2:3:4 std::vector\n[coop] device_token=abc123\nticket=T1\n版本 1.0.0+28 2424x1705");
        for (String bad : new String[] { "lobby.example.invalid", "203.0.113.7", "fe80:0", "abc123", "T1" }) check(!sc.contains(bad), "kept " + bad);
        check(sc.contains("std::vector") && sc.contains("版本 1.0.0+28 2424x1705"), "kept normal text");
        // 私钥文件：0600、格式不对拒用不覆盖 | key file
        File kf = new File(dir, "k"); byte[] k1 = DiagUploadClient.loadOrCreateSeed(kf); check(k1.length == 32 && java.util.Arrays.equals(k1, DiagUploadClient.loadOrCreateSeed(kf)), "stable seed"); check(Files.getPosixFilePermissions(kf.toPath()).equals(java.nio.file.attribute.PosixFilePermissions.fromString("rw-------")), "0600");
        Files.write(kf.toPath(), new byte[5]); try { DiagUploadClient.loadOrCreateSeed(kf); check(false, "bad key accepted"); } catch (IOException e) { check(kf.length() == 5, "not overwritten"); }
        wipe(dir); dir.delete();
        System.out.println("DiagUploadClientTest: " + cases + " checks passed");
    }
}
