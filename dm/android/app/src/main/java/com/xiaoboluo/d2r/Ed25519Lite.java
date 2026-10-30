package com.xiaoboluo.d2r;

import java.math.BigInteger;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Arrays;

// 诊断上传用的 Ed25519 签名（RFC 8032，纯 Java、不依赖 android.*；桌面 JVM 上用 RFC 8032 第 7.1 节测试向量单测：tests/DiagUploadClientTest.java）。
// 为什么不用系统的：java.security 的 Ed25519 要 API 33，本包 minSdk 31；这个身份只证明「同一安装」、不保护账号或存档，不要求恒定时间，一份报告只签一次，BigInteger 够快。
// Ed25519 signing for diagnostic uploads (RFC 8032, pure Java, no android.*; unit-tested on the desktop JVM with the RFC 8032 section 7.1 vectors).
// Why not the system's: java.security Ed25519 needs API 33 and this package's minSdk is 31; this identity only proves "same installation" and protects no account or save, so constant time is not required, and one report is signed once, so BigInteger is fast enough.
final class Ed25519Lite {
    private static final BigInteger TWO = BigInteger.valueOf(2);   // BigInteger.TWO 要 API 33，本包 minSdk 31 | BigInteger.TWO needs API 33, minSdk here is 31
    private static final BigInteger P = BigInteger.ONE.shiftLeft(255).subtract(BigInteger.valueOf(19));
    private static final BigInteger L = BigInteger.ONE.shiftLeft(252).add(new BigInteger("27742317777372353535851937790883648493"));
    private static final BigInteger D = BigInteger.valueOf(-121665).multiply(BigInteger.valueOf(121666).modInverse(P)).mod(P);
    private static final BigInteger I = TWO.modPow(P.subtract(BigInteger.ONE).shiftRight(2), P);
    private static final BigInteger BY = BigInteger.valueOf(4).multiply(BigInteger.valueOf(5).modInverse(P)).mod(P);
    private static final BigInteger[] B = { xRecover(BY), BY, BigInteger.ONE, xRecover(BY).multiply(BY).mod(P) };
    private Ed25519Lite() {}

    private static BigInteger xRecover(BigInteger y) {
        BigInteger yy = y.multiply(y).mod(P), xx = yy.subtract(BigInteger.ONE).multiply(D.multiply(yy).add(BigInteger.ONE).modInverse(P)).mod(P);
        BigInteger x = xx.modPow(P.add(BigInteger.valueOf(3)).shiftRight(3), P);
        if (!x.multiply(x).subtract(xx).mod(P).equals(BigInteger.ZERO)) x = x.multiply(I).mod(P);
        return x.testBit(0) ? P.subtract(x) : x;
    }
    private static BigInteger[] add(BigInteger[] p, BigInteger[] q) {   // 扩展坐标 | extended coordinates
        BigInteger a = p[1].subtract(p[0]).multiply(q[1].subtract(q[0])).mod(P), b = p[1].add(p[0]).multiply(q[1].add(q[0])).mod(P);
        BigInteger c = p[3].multiply(TWO).multiply(D).multiply(q[3]).mod(P), d = p[2].multiply(TWO).multiply(q[2]).mod(P);
        BigInteger e = b.subtract(a), f = d.subtract(c), g = d.add(c), h = b.add(a);
        return new BigInteger[] { e.multiply(f).mod(P), g.multiply(h).mod(P), f.multiply(g).mod(P), e.multiply(h).mod(P) };
    }
    private static BigInteger[] mul(BigInteger s, BigInteger[] p) {
        BigInteger[] r = { BigInteger.ZERO, BigInteger.ONE, BigInteger.ONE, BigInteger.ZERO };
        for (int i = s.bitLength() - 1; i >= 0; i--) { r = add(r, r); if (s.testBit(i)) r = add(r, p); }
        return r;
    }
    private static byte[] encode(BigInteger[] p) {
        BigInteger zi = p[2].modInverse(P), x = p[0].multiply(zi).mod(P), y = p[1].multiply(zi).mod(P);
        byte[] o = le(y, 32); if (x.testBit(0)) o[31] |= (byte) 0x80; return o;
    }
    private static byte[] le(BigInteger v, int n) { byte[] o = new byte[n], b = v.toByteArray(); for (int i = 0; i < n && i < b.length; i++) o[i] = b[b.length - 1 - i]; return o; }
    private static BigInteger fromLe(byte[] b) { byte[] r = new byte[b.length + 1]; for (int i = 0; i < b.length; i++) r[b.length - i] = b[i]; return new BigInteger(r); }
    private static byte[] sha512(byte[]... parts) {
        try { MessageDigest m = MessageDigest.getInstance("SHA-512"); for (byte[] p : parts) m.update(p); return m.digest(); }
        catch (NoSuchAlgorithmException e) { throw new IllegalStateException(e); }
    }
    private static BigInteger clamp(byte[] h) { byte[] a = Arrays.copyOf(h, 32); a[0] &= (byte) 248; a[31] &= 127; a[31] |= 64; return fromLe(a); }
    static byte[] publicKey(byte[] seed) { return encode(mul(clamp(sha512(seed)), B)); }
    static byte[] sign(byte[] seed, byte[] message) {
        byte[] h = sha512(seed), pub = encode(mul(clamp(h), B)), prefix = Arrays.copyOfRange(h, 32, 64);
        BigInteger r = fromLe(sha512(prefix, message)).mod(L); byte[] R = encode(mul(r, B));
        BigInteger k = fromLe(sha512(R, pub, message)).mod(L), s = r.add(k.multiply(clamp(h))).mod(L);
        byte[] out = new byte[64]; System.arraycopy(R, 0, out, 0, 32); System.arraycopy(le(s, 32), 0, out, 32, 32); return out;
    }
}
