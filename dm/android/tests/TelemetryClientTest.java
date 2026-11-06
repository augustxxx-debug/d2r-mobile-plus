package com.xiaoboluo.d2r;

import java.nio.charset.StandardCharsets;
import java.security.KeyFactory;
import java.security.Signature;
import java.security.spec.X509EncodedKeySpec;
import java.util.Base64;

// 统计信封（TelemetryClient，P-000）：transcript 前缀、正好五个字段、upload_id 形状且每次新、payload 与公钥编码、签名能验。回话归类在 dm_telemetry.c，单测在 t_telemetry.c。
// The statistics envelope (P-000): transcript prefix, exactly five fields, upload_id shape and new each time, payload and key encoding, a verifiable signature. Classifying answers is in dm_telemetry.c, tested in t_telemetry.c.
public class TelemetryClientTest {
    private static int cases;
    private static void check(boolean ok, String what) { cases++; if (!ok) throw new AssertionError(what); }
    static final String CHALLENGE = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    public static void main(String[] args) throws Exception {
        byte[] seed = new byte[32]; seed[0] = 7; byte[] payload = "{\"schema\":1,\"events\":[{\"seq\":1}]}".getBytes(StandardCharsets.UTF_8);
        String t = new String(TelemetryClient.transcript("c", "i", payload), StandardCharsets.UTF_8);
        check(t.startsWith("D2R-TELEMETRY-V1\nc\ni\n") && t.endsWith("\n") && t.length() == "D2R-TELEMETRY-V1\nc\ni\n".length() + 65, "transcript");
        byte[] body = TelemetryClient.envelope(seed, CHALLENGE, payload), body2 = TelemetryClient.envelope(seed, CHALLENGE, payload);
        String text = new String(body, StandardCharsets.UTF_8);
        for (String k : new String[] { "challenge", "payload", "public_key", "signature", "upload_id" }) check(DiagUploadClient.field(body, k) != null, "field " + k);
        check(text.chars().filter(c -> c == ':').count() == 5, "exactly five fields");
        String id = DiagUploadClient.field(body, "upload_id");
        check(id.matches("[0-9a-f]{32}"), "upload_id shape");
        check(!id.equals(DiagUploadClient.field(body2, "upload_id")), "a new upload_id each time");
        check(DiagUploadClient.field(body, "challenge").equals(CHALLENGE), "challenge as given");
        check(DiagUploadClient.field(body, "payload").equals(DiagUploadClient.b64(payload)), "payload");
        check(DiagUploadClient.field(body, "public_key").equals(DiagUploadClient.b64(Ed25519Lite.publicKey(seed))), "public key");
        for (String k : new String[] { "payload", "public_key", "signature" }) check(DiagUploadClient.field(body, k).matches("[A-Za-z0-9_-]+"), k + " is base64url without padding");
        byte[] pub = Ed25519Lite.publicKey(seed), der = new byte[44]; byte[] head = java.util.HexFormat.of().parseHex("302a300506032b6570032100"); System.arraycopy(head, 0, der, 0, 12); System.arraycopy(pub, 0, der, 12, 32);
        Signature v = Signature.getInstance("Ed25519"); v.initVerify(KeyFactory.getInstance("Ed25519").generatePublic(new X509EncodedKeySpec(der)));
        v.update(TelemetryClient.transcript(CHALLENGE, id, payload));
        check(v.verify(Base64.getUrlDecoder().decode(DiagUploadClient.field(body, "signature"))), "signature verifies");
        System.out.println("TelemetryClientTest: " + cases + " checks passed");
    }
}
