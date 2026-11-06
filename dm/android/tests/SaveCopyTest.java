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
        // 选文件的副本名（B175）| the picked copy's name
        check(SaveCopy.pickName("Alice.d2s", "application/octet-stream").equals("Alice.d2s") && SaveCopy.pickName("SharedStashSoftCoreV2.D2I", null).equals("SharedStashSoftCoreV2.D2I"), "角色 / 仓库留原名（不分大小写）");
        check(SaveCopy.pickName("D2RMobilePlus_存档_20261105-1430_TestPhone.zip", null).equals("save_import_pick.zip") && SaveCopy.pickName("x.ZIP", null).equals("save_import_pick.zip"), ".zip 当包");
        check(SaveCopy.pickName("download", "application/zip").equals("save_import_pick.zip") && SaveCopy.pickName(null, null).equals("save_import_pick.zip"), "MIME 是 zip、或名字问不到也当包");
        check(SaveCopy.pickName("photo.jpg", "image/jpeg").equals("save_import_pick.bin") && SaveCopy.pickName("notes.txt", "application/octet-stream").equals("save_import_pick.bin"), "别的类型不冒充 .zip：宿主看文件头给类型提示");
        check(SaveCopy.pickName("../evil.d2s", null).equals("save_import_pick.bin") && SaveCopy.pickName(".hidden.d2s", null).equals("save_import_pick.bin"), "带路径或点开头的名字不用原名");
        java.util.List<String> pm = java.util.Arrays.asList(SaveCopy.PICK_MIME); check(pm.contains("application/zip") && pm.contains("application/octet-stream") && !pm.contains("*/*"), "面板类型表只含存档相关");
        // 每次选择一个新目录，旧副本清掉；拷贝排成一个线程（审计 20261104 P1-1）| a new directory per pick, older copies cleared; copies run on one thread
        java.io.File par = java.nio.file.Files.createTempDirectory("savecopy").toFile();
        java.io.File d1 = SaveCopy.freshDir(par); new java.io.FileOutputStream(new java.io.File(d1, "a.d2s")).close(); java.io.File d2 = SaveCopy.freshDir(par);
        check(!d1.equals(d2) && d2.isDirectory() && !d1.exists() && d2.getParentFile().equals(par) && !d2.equals(par), "两次选择不共用目录，旧的清掉");
        java.util.concurrent.atomic.AtomicInteger in = new java.util.concurrent.atomic.AtomicInteger(), peak = new java.util.concurrent.atomic.AtomicInteger();
        java.util.List<java.util.concurrent.Future<?>> fs = new java.util.ArrayList<>(); for (int i = 0; i < 6; i++) fs.add(SaveCopy.PICKS.submit(() -> { int k = in.incrementAndGet(); peak.accumulateAndGet(k, Math::max); try { Thread.sleep(20); } catch (InterruptedException ignored) {} in.decrementAndGet(); }));
        for (java.util.concurrent.Future<?> f : fs) f.get(); check(peak.get() == 1, "选文件的拷贝同时只有一个 [" + peak.get() + "]");
        SaveCopy.deleteTree(par); check(!par.exists(), "清理");
        System.out.println("PASS SaveCopyTest");
    }
}
