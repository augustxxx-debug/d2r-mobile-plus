package com.xiaoboluo.d2r;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

// 设备与使用统计一批的网络部分（iOS 版 dm_diag_upload.swift telemetryBatch；策略 dm/host/dm_telemetry.c）。不依赖 android.*（桌面 JVM 单测 tests/TelemetryClientTest.java）。与崩溃上报同一套挑战签名与 HTTP（DiagUploadClient.Http、Ed25519Lite），接口与 transcript 前缀不同；私钥是另一把。
// 结果码同 DM_DIAGUP_*：2xx、409 = 已送达；429 / 503 照 Retry-After；408、其他 5xx 可重试；其他 4xx 整批丢弃。
// The network part of one statistics batch (iOS: telemetryBatch in dm_diag_upload.swift; policy: dm_telemetry.c). No android.* (unit-tested on the desktop JVM). The same challenge signing and HTTP as crash reports with other endpoints and transcript prefix; a separate private key.
// Result codes as DM_DIAGUP_*: 2xx, 409 delivered; 429 / 503 follow Retry-After; 408 and other 5xx retryable; other 4xx drop the batch.
final class TelemetryClient {
    private TelemetryClient() {}
    private static final int MAX_RETRY_AFTER = 7 * 86400;
    private static byte[] sha256(byte[] d) { try { return MessageDigest.getInstance("SHA-256").digest(d); } catch (NoSuchAlgorithmException e) { throw new IllegalStateException(e); } }
    static byte[] transcript(String challenge, String id, byte[] payload) { return ("D2R-TELEMETRY-V1\n" + challenge + "\n" + id + "\n" + DiagUploadClient.hex(sha256(payload)) + "\n").getBytes(StandardCharsets.UTF_8); }
    private static String code(DiagUploadClient.Response r, String dflt) { String c = DiagUploadClient.field(r.body, "error"); return c != null && c.matches("[a-z0-9_]{1,64}") ? c : dflt; }
    static DiagUploadClient.Result classify(DiagUploadClient.Response r) {
        int s = r.code, after = Math.min(Math.max(r.retryAfter, 0), MAX_RETRY_AFTER);
        if (s >= 200 && s < 300) return new DiagUploadClient.Result(DiagUploadClient.OK, "ok", 0, 0);
        if (s == 409) return new DiagUploadClient.Result(DiagUploadClient.CONFLICT, code(r, "duplicate"), 0, 0);
        if (s == 429) return new DiagUploadClient.Result(DiagUploadClient.RATE_LIMITED, code(r, "rate_limited"), 0, after > 0 ? after : 60);
        if (s == 503) return new DiagUploadClient.Result(DiagUploadClient.MAINTENANCE, code(r, "unavailable"), 0, after > 0 ? after : 60);
        if (s == 408 || (s >= 500 && s < 600)) return new DiagUploadClient.Result(DiagUploadClient.UNAVAILABLE, code(r, "http_" + s), 0, 0);
        return new DiagUploadClient.Result(DiagUploadClient.REJECTED, code(r, "http_" + s), 0, 0);
    }
    static DiagUploadClient.Result batch(DiagUploadClient.Http http, byte[] seed, byte[] payload) {
        String id = DiagUploadClient.newId();
        for (int round = 0; round < 2; round++) {
            DiagUploadClient.Response ch;
            try { ch = http.send("GET", "challenge", null); } catch (IOException e) { return new DiagUploadClient.Result(DiagUploadClient.UNAVAILABLE, "network", 0, 0); }
            if (ch.code != 200) return classify(ch);
            String challenge = DiagUploadClient.field(ch.body, "challenge");
            if (challenge == null || !challenge.matches("[A-Za-z0-9_-]{43}")) return new DiagUploadClient.Result(DiagUploadClient.UNAVAILABLE, "bad_challenge", 0, 0);
            byte[] sig = Ed25519Lite.sign(seed, transcript(challenge, id, payload));
            byte[] body = ("{\"challenge\":\"" + challenge + "\",\"payload\":\"" + DiagUploadClient.b64(payload) + "\",\"public_key\":\"" + DiagUploadClient.b64(Ed25519Lite.publicKey(seed)) + "\",\"signature\":\"" + DiagUploadClient.b64(sig) + "\",\"upload_id\":\"" + id + "\"}").getBytes(StandardCharsets.UTF_8);
            DiagUploadClient.Response r;
            try { r = http.send("POST", "events", body); } catch (IOException e) { return new DiagUploadClient.Result(DiagUploadClient.UNCONFIRMED, "response_lost", 0, 0); }
            if (r.code == 403 && "challenge_rejected".equals(code(r, ""))) continue;   // 挑战在签名后失效：重取一次 | the challenge expired after signing: one fresh one
            return classify(r);
        }
        return new DiagUploadClient.Result(DiagUploadClient.REJECTED, "challenge_rejected", 0, 0);
    }
}
