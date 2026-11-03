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
        // 选文件夹的预算：数量封顶（同原生 MAX_FILES）、总量封顶、扣的是实写字节 | the folder budget: capped by count (native MAX_FILES) and bytes, charged by the bytes written
        SaveCopy.Budget b = new SaveCopy.Budget(SaveCopy.PICK_MAX);
        for (int i = 0; i < SaveCopy.Budget.MAX_FILES; i++) { check(b.fits(0), "第 " + i + " 份还在数量预算里"); b.spend(0); }
        check(!b.fits(0) && b.filesLeft() == 0, "到 4096 份就停");
        b = new SaveCopy.Budget(1000); check(b.fits(1000) && !b.fits(1001), "总量按字节封顶");
        b.spend(1000); check(!b.fits(1) && b.bytesLeft() == 0, "扣完就不再有");
        b = new SaveCopy.Budget(1000); b.spend(1500); check(b.bytesLeft() < 0 && !b.fits(0), "实写比声明多：下一份就停，不会再超");
        b = new SaveCopy.Budget(Long.MAX_VALUE); check(!b.fits(-1), "大小不明（负数）算放不下");
        System.out.println("PASS SaveCopyTest");
    }
}
