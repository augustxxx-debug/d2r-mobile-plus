package com.xiaoboluo.d2r;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

// 设备与使用统计的网络部分（iOS 版在 dm_diag_upload.swift，策略与回话归类都在 dm/host/dm_telemetry.c）。没有自动重试：挑战、提交各发一次，回话原样交回（P-000）。
// The network part of the statistics (iOS: dm_diag_upload.swift; policy and classification of answers: dm/host/dm_telemetry.c). No automatic retries: one challenge, one submit, answers go back as they are (P-000).
final class TelemetryClient {
    private TelemetryClient() {}
    private static byte[] sha256(byte[] d) { try { return MessageDigest.getInstance("SHA-256").digest(d); } catch (NoSuchAlgorithmException e) { throw new IllegalStateException(e); } }
    static byte[] transcript(String challenge, String id, byte[] payload) { return ("D2R-TELEMETRY-V1\n" + challenge + "\n" + id + "\n" + DiagUploadClient.hex(sha256(payload)) + "\n").getBytes(StandardCharsets.UTF_8); }
    // 信封五个字段；每次新 upload_id（response_lost 后事件 ID 不变，服务端按重复回） | the five envelope fields; a new upload_id each time (after response_lost the event IDs stay, the server answers duplicate)
    static byte[] envelope(byte[] seed, String challenge, byte[] payload) {
        String id = DiagUploadClient.newId(); byte[] sig = Ed25519Lite.sign(seed, transcript(challenge, id, payload));
        return ("{\"challenge\":\"" + challenge + "\",\"payload\":\"" + DiagUploadClient.b64(payload) + "\",\"public_key\":\"" + DiagUploadClient.b64(Ed25519Lite.publicKey(seed)) + "\",\"signature\":\"" + DiagUploadClient.b64(sig) + "\",\"upload_id\":\"" + id + "\"}").getBytes(StandardCharsets.UTF_8);
    }
}
