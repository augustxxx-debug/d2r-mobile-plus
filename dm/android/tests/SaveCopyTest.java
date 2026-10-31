package com.xiaoboluo.d2r;

import java.io.InputStream;
import java.io.OutputStream;

/** SaveCopy 的边界（外审 S-09）：只数字节，不写实体文件。| SaveCopy's boundary: counts bytes, writes no real file. */
public final class SaveCopyTest {
    static final class Zeros extends InputStream { long left; Zeros(long n) { left = n; } @Override public int read() { return left-- > 0 ? 0 : -1; } @Override public int read(byte[] b, int o, int l) { if (left <= 0) return -1; int k = (int) Math.min(left, l); left -= k; return k; } }
    static final class Count extends OutputStream { long n; @Override public void write(int b) { n++; } @Override public void write(byte[] b, int o, int l) { n += l; } }
    static void check(boolean c, String what) { if (!c) throw new AssertionError(what); }
    public static void main(String[] a) throws Exception {
        Count c = new Count(); check(SaveCopy.copy(new Zeros(SaveCopy.PICK_MAX), c, SaveCopy.PICK_MAX) && c.n == SaveCopy.PICK_MAX, "正好到上限要成功");
        c = new Count(); check(!SaveCopy.copy(new Zeros(SaveCopy.PICK_MAX + 1), c, SaveCopy.PICK_MAX) && c.n <= SaveCopy.PICK_MAX, "超上限要停，不多写");
        c = new Count(); check(SaveCopy.copy(new Zeros(SaveCopy.PICK_MAX + 1), c, Long.MAX_VALUE), "导出方向不限");
        check(!SaveCopy.copy(null, new Count(), 1) && !SaveCopy.copy(new Zeros(1), null, 1), "打不开流算失败");
        System.out.println("PASS SaveCopyTest");
    }
}
