package com.xiaoboluo.d2r;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/** 存档包在系统面板和应用目录之间的拷贝。单独一个类，不依赖安卓，单测直接跑（dm/host/tests/test_savepkg_gates.py）。| Copies a save package between the system panel and the app's directory. A class of its own with no Android dependency, so the unit test runs it directly. */
final class SaveCopy {
    /** 与原生 zf_open 的上限相同：选错大文件不把盘写满。| Same as the native zf_open limit: picking a huge file by mistake must not fill the disk. */
    static final long PICK_MAX = 320L << 20;
    private SaveCopy() {}
    /** 选进来的副本一次拷一份（单线程），两次选择不会同时写、互删同一个目录（审计 20261104 P1-1）。| Picked copies are made one at a time (one thread), so two picks never write or delete the same directory at once. */
    static final java.util.concurrent.ExecutorService PICKS = java.util.concurrent.Executors.newSingleThreadExecutor(r -> { Thread t = new Thread(r, "dm-save-pick"); t.setDaemon(true); return t; });
    private static final java.util.concurrent.atomic.AtomicLong SEQ = new java.util.concurrent.atomic.AtomicLong();
    /** 这次选择自己的新目录（parent 下）：先清掉以前的副本——能开系统面板时宿主没有后台活，旧副本已经没人读；在 PICKS 上调。| A new directory of this pick's own under parent. Older copies are cleared first: the panel only opens while the host has no job, so nothing reads them any more. Call on PICKS. */
    static File freshDir(File parent) throws IOException {
        File[] old = parent.listFiles(); if (old != null) for (File c : old) deleteTree(c);
        File d = new File(parent, "p" + SEQ.incrementAndGet() + "-" + System.nanoTime()); if (!d.mkdirs()) throw new IOException("mkdir " + d); return d;
    }
    static void deleteTree(File f) { File[] k = f.listFiles(); if (k != null) for (File c : k) deleteTree(c); f.delete(); }
    /** 超过 max 就停（返回 false，已写的由调用方删）。| Stops past max (returns false; the caller deletes what was written). */
    static boolean copy(InputStream in, OutputStream out, long max) throws IOException {
        if (in == null || out == null) return false;
        byte[] buf = new byte[1 << 16]; int n; long total = 0;
        while ((n = in.read(buf)) > 0) { total += n; if (total > max) return false; out.write(buf, 0, n); }
        out.flush(); return true;
    }
    /** 选文件面板只给看存档相关的类型（B175）：ZIP 的几种叫法，加上 octet-stream（.d2s / .d2i 没有登记的 MIME，provider 一般报这个）。图片、视频、文档在面板里是灰的。| The pick panel shows save-related types only: the names a ZIP goes by, plus octet-stream (.d2s / .d2i have no registered MIME, providers usually report this one). Pictures, videos and documents are greyed out. */
    static final String[] PICK_MIME = { "application/zip", "application/x-zip-compressed", "application/x-zip", "application/octet-stream" };
    /** 拷进来的副本叫什么：.d2s / .d2i 留原名（宿主按名字分流）；叫 .zip、MIME 是 zip、或名字问不到的当包；其余叫 .bin，宿主看文件头，不是 ZIP 就给「选的不是存档」（B175）。| The copy's name: a .d2s / .d2i keeps its own (the host goes by the name); a .zip name, a zip MIME or an unknown name is a package; anything else is .bin, and the host reads its header: not a ZIP means "that is not a save". */
    static String pickName(String nm, String mime) {
        String lo = nm == null ? null : nm.toLowerCase(java.util.Locale.ROOT);
        if (lo != null && !nm.contains("/") && !nm.startsWith(".") && (lo.endsWith(".d2s") || lo.endsWith(".d2i"))) return nm;
        if (lo == null || lo.endsWith(".zip") || (mime != null && mime.toLowerCase(java.util.Locale.ROOT).contains("zip"))) return "save_import_pick.zip";
        return "save_import_pick.bin";
    }
    /** 选文件夹时的副本预算：文件数（同原生 MAX_FILES）与总量，逐文件扣，选错目录时整份放弃。| The copy's budget when a folder is picked: file count (native MAX_FILES) and total size, spent file by file, so a wrong pick is given up as a whole. */
    static final class Budget {
        static final int MAX_FILES = 4096;
        private int files = MAX_FILES; private long bytes;
        Budget(long maxBytes) { bytes = maxBytes; }
        /** 还能不能再要一份：数量或总量到顶就 false。| Whether the next one still fits: false once the count or the bytes run out. */
        boolean fits(long size) { return size >= 0 && files > 0 && size <= bytes; }
        /** 拷完扣账：按实际字节算（系统报的大小可能比实际小）。| Charges after the copy, by the bytes actually written (the size the system reports can be short). */
        void spend(long size) { files--; bytes -= size < 0 ? 0 : size; }
        int filesLeft() { return files; }
        long bytesLeft() { return bytes; }
    }
}
