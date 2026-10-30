package com.xiaoboluo.d2r;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;

// 统计一批的协议（TelemetryClient）：transcript 前缀、请求体五个字段、签名能验、结果归类（2xx / 409 / 429 / 503 / 408 / 5xx / 4xx）、挑战失效重取一次、没有网络。
// One statistics batch: transcript prefix, five body fields, a verifiable signature, result classes, one fresh challenge after expiry, no network.
public class TelemetryClientTest {
    private static int cases;
    private static void check(boolean ok, String what) { cases++; if (!ok) throw new AssertionError(what); }
    interface Answer { DiagUploadClient.Response on(String m, String p, byte[] b) throws IOException; }
    static final class Fake implements DiagUploadClient.Http {
        final ArrayDeque<Answer> script = new ArrayDeque<>(); final List<byte[]> posts = new ArrayList<>(); final List<String> paths = new ArrayList<>();
        @Override public DiagUploadClient.Response send(String m, String p, byte[] b) throws IOException { paths.add(m + " " + p); if (b != null) posts.add(b); Answer a = script.poll(); if (a == null) throw new IOException("no script"); return a.on(m, p, b); }
    }
    static final String CHALLENGE = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    static Answer reply(int code, String json, int retry) { return (m, p, b) -> new DiagUploadClient.Response(code, json.getBytes(StandardCharsets.UTF_8), retry); }
    static Answer challenge() { return reply(200, "{\"challenge\":\"" + CHALLENGE + "\",\"expires_in\":120}", 0); }
    public static void main(String[] args) throws Exception {
        byte[] seed = new byte[32]; seed[0] = 7; byte[] payload = "{\"schema\":1,\"events\":[{\"seq\":1}]}".getBytes(StandardCharsets.UTF_8);
        check(new String(TelemetryClient.transcript("c", "i", payload), StandardCharsets.UTF_8).startsWith("D2R-TELEMETRY-V1\nc\ni\n"), "transcript prefix");
        Fake f = new Fake(); f.script.add(challenge()); f.script.add(reply(200, "{\"accepted\":1}", 0));
        DiagUploadClient.Result r = TelemetryClient.batch(f, seed, payload);
        check(r.outcome == DiagUploadClient.OK && f.paths.equals(List.of("GET challenge", "POST events")), "round trip");
        byte[] body = f.posts.get(0); for (String k : new String[] { "challenge", "payload", "public_key", "signature", "upload_id" }) check(DiagUploadClient.field(body, k) != null, "field " + k);
        check(DiagUploadClient.field(body, "upload_id").matches("[0-9a-f]{32}") && DiagUploadClient.field(body, "payload").equals(DiagUploadClient.b64(payload)) && DiagUploadClient.field(body, "public_key").equals(DiagUploadClient.b64(Ed25519Lite.publicKey(seed))), "values");
        int[][] table = { { 201, DiagUploadClient.OK, 0, 0 }, { 409, DiagUploadClient.CONFLICT, 0, 0 }, { 429, DiagUploadClient.RATE_LIMITED, 30, 30 }, { 429, DiagUploadClient.RATE_LIMITED, 0, 60 }, { 503, DiagUploadClient.MAINTENANCE, 86400, 86400 }, { 503, DiagUploadClient.MAINTENANCE, 99999999, 604800 }, { 408, DiagUploadClient.UNAVAILABLE, 0, 0 }, { 500, DiagUploadClient.UNAVAILABLE, 0, 0 }, { 400, DiagUploadClient.REJECTED, 0, 0 }, { 413, DiagUploadClient.REJECTED, 0, 0 } };
        for (int[] t : table) { Fake g = new Fake(); g.script.add(challenge()); g.script.add(reply(t[0], "{}", t[2])); r = TelemetryClient.batch(g, seed, payload); check(r.outcome == t[1] && r.retryAfter == t[3], "status " + t[0] + " → " + r.outcome + " / " + r.retryAfter); }
        // 取挑战就 503（暂停）：不提交 | 503 at the challenge (paused): nothing submitted
        Fake p = new Fake(); p.script.add(reply(503, "{\"error\":\"uploads_paused\",\"paused\":true}", 3600)); r = TelemetryClient.batch(p, seed, payload); check(r.outcome == DiagUploadClient.MAINTENANCE && r.retryAfter == 3600 && r.detail.equals("uploads_paused") && p.posts.isEmpty(), "paused");
        // 挑战失效：重取一次，同一 upload_id | an expired challenge: one fresh one, same upload_id
        Fake e = new Fake(); e.script.add(challenge()); e.script.add(reply(403, "{\"error\":\"challenge_rejected\"}", 0)); e.script.add(challenge()); e.script.add(reply(200, "{}", 0)); r = TelemetryClient.batch(e, seed, payload);
        check(r.outcome == DiagUploadClient.OK && e.posts.size() == 2 && DiagUploadClient.field(e.posts.get(0), "upload_id").equals(DiagUploadClient.field(e.posts.get(1), "upload_id")), "refetch");
        Fake e2 = new Fake(); for (int i = 0; i < 2; i++) { e2.script.add(challenge()); e2.script.add(reply(403, "{\"error\":\"challenge_rejected\"}", 0)); } r = TelemetryClient.batch(e2, seed, payload); check(r.outcome == DiagUploadClient.REJECTED && e2.posts.size() == 2, "refetch once");
        // 网络错误 | network errors
        Fake n = new Fake(); r = TelemetryClient.batch(n, seed, payload); check(r.outcome == DiagUploadClient.UNAVAILABLE, "offline");
        Fake l = new Fake(); l.script.add(challenge()); r = TelemetryClient.batch(l, seed, payload); check(r.outcome == DiagUploadClient.UNCONFIRMED, "lost");
        System.out.println("TelemetryClientTest: " + cases + " checks passed");
    }
}
