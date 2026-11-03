package com.xiaoboluo.d2r;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/** 存档包在系统面板和应用目录之间的拷贝。单独一个类，不依赖安卓，单测直接跑（dm/host/tests/test_savepkg_gates.py）。| Copies a save package between the system panel and the app's directory. A class of its own with no Android dependency, so the unit test runs it directly. */
final class SaveCopy {
    /** 与原生 zf_open 的上限相同：选错大文件不把盘写满。| Same as the native zf_open limit: picking a huge file by mistake must not fill the disk. */
    static final long PICK_MAX = 320L << 20;
    private SaveCopy() {}
    /** 超过 max 就停（返回 false，已写的由调用方删）。| Stops past max (returns false; the caller deletes what was written). */
    static boolean copy(InputStream in, OutputStream out, long max) throws IOException {
        if (in == null || out == null) return false;
        byte[] buf = new byte[1 << 16]; int n; long total = 0;
        while ((n = in.read(buf)) > 0) { total += n; if (total > max) return false; out.write(buf, 0, n); }
        out.flush(); return true;
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
