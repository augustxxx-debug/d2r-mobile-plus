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
}
