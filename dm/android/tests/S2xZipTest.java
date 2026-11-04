package com.xiaoboluo.d2r;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Random;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

// S2xZip 的单测：直接用 java.util.zip.ZipOutputStream 在临时目录造包，不依赖 Android SDK，也不碰真机 / adb。
// Unit tests for S2xZip: packs are built with java.util.zip.ZipOutputStream in a temporary directory; no Android SDK and no device / adb.
//
// 覆盖：找包（大小写、优先级）、外层多一级目录、无外层、缺 .build.config、无点 build.config 改名、点开头文件、zip slip（../、绝对路径、符号链接）、
// 续传（中途取消后再次 extract）、CRC 坏的同尺寸已存在文件重写、zip64（70000 个空条目）、总字节数与 spaceNeeded、进度回调。
// Covers: find (case, priority), an extra outer directory, no outer directory, missing .build.config, renaming a dotless build.config, dot files,
// zip slip (../, absolute paths, symlinks), resume (cancel mid-way then extract again), rewriting an existing same-size file with a bad CRC,
// zip64 (70000 empty entries), total bytes / spaceNeeded, and the progress callback.
public class S2xZipTest {
    static int cases;
    static File root;

    static final byte[] MAIN = bytes("main!");
    static final byte[] SDK = bytes("sdk!");
    static final byte[] CONFIG = bytes("config=1\n");
    static final byte[] DATA = bytes("1234");

    static final long MARGIN = 200L << 20;
    static final int ZIP64_ENTRIES = 70000;

    interface ZipMaker { void make(ZipOutputStream z) throws IOException; }

    static void ok(boolean cond, String what) {
        cases++;
        if (!cond) throw new AssertionError("case " + cases + ": " + what);
    }

    static void equal(Object want, Object actual) {
        cases++;
        if (want == null ? actual != null : !want.equals(actual))
            throw new AssertionError("case " + cases + ": want=" + want + " actual=" + actual);
    }

    static void sameBytes(byte[] want, byte[] actual, String what) {
        cases++;
        if (!Arrays.equals(want, actual)) throw new AssertionError("case " + cases + ": " + what);
    }

    static byte[] bytes(String s) { return s.getBytes(StandardCharsets.UTF_8); }

    static File dir(String name) throws IOException {
        File d = new File(root, name);
        if (!d.isDirectory() && !d.mkdirs() && !d.isDirectory()) throw new IOException("mkdir " + d);
        return d;
    }

    static void write(File f, String text) throws IOException { write(f, bytes(text)); }

    static void write(File f, byte[] data) throws IOException {
        File p = f.getParentFile();
        if (p != null && !p.isDirectory() && !p.mkdirs()) throw new IOException("mkdir " + p);
        Files.write(f.toPath(), data);
    }

    static String text(File f) throws IOException { return new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8); }

    static File zip(String name, ZipMaker maker) throws IOException {
        File f = new File(root, name);
        File p = f.getParentFile();
        if (p != null && !p.isDirectory() && !p.mkdirs()) throw new IOException("mkdir " + p);
        try (ZipOutputStream z = new ZipOutputStream(new FileOutputStream(f))) {
            maker.make(z);
        }
        return f;
    }

    static void put(ZipOutputStream z, String name, byte[] data) throws IOException {
        z.putNextEntry(new ZipEntry(name));
        z.write(data);
        z.closeEntry();
    }

    static void put(ZipOutputStream z, String name, String data) throws IOException { put(z, name, bytes(data)); }

    static void emptyEntry(ZipOutputStream z, String name) throws IOException {
        z.putNextEntry(new ZipEntry(name.endsWith("/") ? name : name + "/"));
        z.closeEntry();
    }

    // 一份完整游戏文件（前置目录 pre 空或 "S2X/"），两种 build.config 写法 | a complete set of game files (prefix empty or "S2X/"), either build.config spelling
    static void game(ZipOutputStream z, String pre, boolean dotted) throws IOException {
        put(z, pre + "ExeFS/main", MAIN);
        put(z, pre + "ExeFS/sdk", SDK);
        put(z, pre + "data/" + (dotted ? ".build.config" : "build.config"), CONFIG);
        put(z, pre + "data/00.data", DATA);
    }

    static void deleteTree(File f) throws IOException {
        File[] list = f.isDirectory() ? f.listFiles() : null;
        if (list != null) for (File c : list) deleteTree(c);
        if (f.exists() && !f.delete()) throw new IOException("delete " + f);
    }

    // 把中央目录里某个条目的外部属性改成 Unix 符号链接（S_IFLNK | 0777）。ZipOutputStream 没有这个 API，只能事后补。
    // Patch one central-directory entry's external attributes to a Unix symlink (S_IFLNK | 0777); ZipOutputStream has no API for this.
    static void markSymlink(File zip, String name) throws IOException {
        byte[] b = Files.readAllBytes(zip.toPath());
        int eocd = -1;
        for (int i = b.length - 22; i >= 0; i--) if (le32(b, i) == 0x06054b50L) { eocd = i; break; }
        ok(eocd >= 0, "end of central directory found");
        int count = le16(b, eocd + 10);
        int pos = (int) le32(b, eocd + 16);
        boolean patched = false;
        for (int k = 0; k < count; k++) {
            equal(0x02014b50L, le32(b, pos));   // 中央目录头 | central directory header
            int nlen = le16(b, pos + 28), elen = le16(b, pos + 30), clen = le16(b, pos + 32);
            String n = new String(b, pos + 46, nlen, StandardCharsets.UTF_8);
            if (n.equals(name)) { put32(b, pos + 38, 0xA1FF0000); patched = true; }
            pos += 46 + nlen + elen + clen;
        }
        ok(patched, "symlink entry " + name + " patched");
        Files.write(zip.toPath(), b);
    }

    static long le32(byte[] b, int off) {
        return (b[off] & 0xFFL) | ((b[off + 1] & 0xFFL) << 8) | ((b[off + 2] & 0xFFL) << 16) | ((b[off + 3] & 0xFFL) << 24);
    }

    static int le16(byte[] b, int off) { return (b[off] & 0xFF) | ((b[off + 1] & 0xFF) << 8); }

    static void put32(byte[] b, int off, int v) {
        b[off] = (byte) v;
        b[off + 1] = (byte) (v >>> 8);
        b[off + 2] = (byte) (v >>> 16);
        b[off + 3] = (byte) (v >>> 24);
    }

    public static void main(String[] args) throws Exception {
        root = Files.createTempDirectory("dm-s2x-zip").toFile();
        try {
            findCases();
            outerPrefixCase();
            flatCase();
            missingConfigCase();
            dotlessConfigCase();
            zipSlipCases();
            cancelResumeCase();
            badCrcExistingCase();
            zip64Case();
            System.out.println("S2xZip OK (" + cases + " cases)");
        } finally {
            deleteTree(root);
        }
    }

    // 1. 找包：s2x.ZIP 这种大小写也能找到；三个名字都在时按 S2X.zip → D2R Mobile.zip → D2R Mobile Plus.zip
    // 1. find: a lowercase s2x.ZIP is found; with all three present S2X.zip -> D2R Mobile.zip -> D2R Mobile Plus.zip
    static void findCases() throws IOException {
        File dl = dir("find");
        write(new File(dl, "s2x.ZIP"), "x");
        equal(new File(dl, "s2x.ZIP"), S2xZip.find(dl));
        // macOS 默认文件系统不分大小写，S2X.zip 与 s2x.ZIP 不能放在同一目录 | macOS is case-insensitive by default, so S2X.zip and s2x.ZIP cannot share a directory
        File priority = dir("find/priority");
        write(new File(priority, "D2R Mobile Plus.zip"), "x");
        write(new File(priority, "D2R Mobile.zip"), "x");
        write(new File(priority, "S2X.zip"), "x");
        equal(new File(priority, "S2X.zip"), S2xZip.find(priority));
        File dl2 = dir("find/old");
        write(new File(dl2, "D2R Mobile Plus.zip"), "x");
        write(new File(dl2, "D2R Mobile.zip"), "x");
        equal(new File(dl2, "D2R Mobile.zip"), S2xZip.find(dl2));
        equal(null, S2xZip.find(dir("find/empty")));
        equal(null, S2xZip.find(new File(root, "find/does-not-exist")));
    }

    // 2. 外层多一级目录：S2X/ 剥掉再解；总字节数 = 四个文件；完成后 spaceNeeded 只剩 200 MB 余量
    // 2. extra outer directory: S2X/ is stripped; total bytes = the four files; after completion spaceNeeded is just the 200 MB margin
    static void outerPrefixCase() throws IOException {
        File z = zip("outer.zip", x -> game(x, "S2X/", true));
        S2xZip.Plan p = S2xZip.plan(z);
        equal("S2X/", p.prefix);
        ok(p.ok(), "outer pack is structurally fine");
        equal(4, p.files.size());
        long total = MAIN.length + SDK.length + CONFIG.length + DATA.length;
        equal(total, p.totalBytes);
        File empty = dir("outer-empty");
        equal(total + MARGIN, S2xZip.spaceNeeded(p, empty));

        File dest = dir("outer-dest");
        equal(S2xZip.Result.OK, S2xZip.extract(p, dest, null, null));
        equal("main!", text(new File(dest, "ExeFS/main")));
        equal("sdk!", text(new File(dest, "ExeFS/sdk")));
        equal("config=1\n", text(new File(dest, "data/.build.config")));
        equal("1234", text(new File(dest, "data/00.data")));
        equal(MARGIN, S2xZip.spaceNeeded(p, dest));
    }

    // 3. 无外层：直接以 ExeFS/、data/ 开头；进度回调最后一次报满
    // 3. no outer directory: entries start with ExeFS/ and data/; the last progress report is a full one
    static void flatCase() throws IOException {
        File z = zip("flat.zip", x -> game(x, "", true));
        S2xZip.Plan p = S2xZip.plan(z);
        equal("", p.prefix);
        ok(p.ok(), "flat pack is structurally fine");
        File dest = dir("flat-dest");
        final long[] lastDone = {-1}, lastTotal = {-1};
        final String[] last = {null};
        S2xZip.Result r = S2xZip.extract(p, dest, (done, total, cur) -> {
            lastDone[0] = done; lastTotal[0] = total; last[0] = cur;
        }, new AtomicBoolean(false));
        equal(S2xZip.Result.OK, r);
        equal(p.totalBytes, lastDone[0]);
        equal(p.totalBytes, lastTotal[0]);
        ok(last[0] != null && !last[0].isEmpty(), "progress carries the current file name");
    }

    // 4. 缺 data/.build.config：plan 报出名字且 ok()=false，extract 拒绝执行
    // 4. missing data/.build.config: plan names it, ok() is false and extract refuses to run
    static void missingConfigCase() throws IOException {
        File z = zip("missing.zip", x -> {
            put(x, "S2X/ExeFS/main", MAIN);
            put(x, "S2X/ExeFS/sdk", SDK);
            put(x, "S2X/data/00.data", DATA);
        });
        S2xZip.Plan p = S2xZip.plan(z);
        equal(1, p.missing.size());
        ok(p.missing.contains("data/.build.config"), "missing list names data/.build.config");
        ok(!p.ok(), "plan with a missing required item is not ok");
        equal(S2xZip.Result.FAILED, S2xZip.extract(p, dir("missing-dest"), null, null));
    }

    // 5. 无点的 data/build.config 也认，解出来改名为 data/.build.config（与 PackGenerator 的兼容规则一致）
    // 5. a dotless data/build.config is accepted and extracted as data/.build.config (the same compatibility rule as PackGenerator)
    static void dotlessConfigCase() throws IOException {
        File z = zip("dotless.zip", x -> game(x, "D2R Mobile Plus/", false));
        S2xZip.Plan p = S2xZip.plan(z);
        equal("D2R Mobile Plus/", p.prefix);
        ok(p.ok(), "dotless build.config counts");
        File dest = dir("dotless-dest");
        equal(S2xZip.Result.OK, S2xZip.extract(p, dest, null, null));
        equal("config=1\n", text(new File(dest, "data/.build.config")));
        ok(!new File(dest, "data/build.config").exists(), "the dotless name is not extracted as-is");
    }

    // 6. zip slip：../、绝对路径、符号链接整包拒绝，且不在目标目录外留下任何文件
    // 6. zip slip: ../, absolute paths and symlinks reject the whole pack and never leave a file outside the destination
    static void zipSlipCases() throws IOException {
        File parentEscape = zip("slip-parent.zip", x -> {
            game(x, "", true);
            put(x, "../evil.txt", "evil");
        });
        S2xZip.Plan p = S2xZip.plan(parentEscape);
        ok(p.reject != null, "../ is rejected");
        ok(p.files.isEmpty(), "a rejected plan carries no files");
        File d1 = dir("slip-dest1");
        equal(S2xZip.Result.FAILED, S2xZip.extract(p, d1, null, null));
        ok(!new File(root, "evil.txt").exists(), "nothing escaped upward");

        File absolute = zip("slip-abs.zip", x -> {
            game(x, "", true);
            put(x, "/abs-evil.txt", "evil");
        });
        ok(S2xZip.plan(absolute).reject != null, "an absolute path is rejected");

        File linkZip = zip("slip-link.zip", x -> {
            game(x, "", true);
            put(x, "ExeFS/link", "target");
        });
        markSymlink(linkZip, "ExeFS/link");
        ok(S2xZip.plan(linkZip).reject != null, "a symbolic link entry is rejected");
    }

    // 7. 中途取消 → CANCELLED，已完成的文件保留、.part 删除；再次 extract 续完并逐字节一致
    // 7. cancel mid-way -> CANCELLED, finished files stay, .part is deleted; a second extract finishes and the bytes match
    static void cancelResumeCase() throws IOException {
        byte[] big = new byte[4 << 20];
        new Random(7).nextBytes(big);
        File z = zip("cancel.zip", x -> {
            put(x, "ExeFS/main", MAIN);
            put(x, "ExeFS/sdk", SDK);
            put(x, "data/.build.config", CONFIG);
            put(x, "data/99.data", big);
        });
        S2xZip.Plan p = S2xZip.plan(z);
        ok(p.ok(), "cancel pack is structurally fine");
        File dest = dir("cancel-dest");
        AtomicBoolean cancel = new AtomicBoolean(false);
        final int[] reports = {0};
        S2xZip.Result r = S2xZip.extract(p, dest, (done, total, cur) -> {
            reports[0]++;
            cancel.set(true);   // 第一次进度回调就取消 | cancel on the first progress report
        }, cancel);
        equal(S2xZip.Result.CANCELLED, r);
        ok(reports[0] > 0, "progress was reported before the cancel");
        ok(!new File(dest, "data/99.data.part").exists(), "the .part file is deleted on cancel");
        ok(!new File(dest, "data/99.data").exists(), "the cancelled file is not renamed into place");
        equal("main!", text(new File(dest, "ExeFS/main")));
        equal("config=1\n", text(new File(dest, "data/.build.config")));
        long finished = MAIN.length + SDK.length + CONFIG.length;
        equal(p.totalBytes - finished + MARGIN, S2xZip.spaceNeeded(p, dest));

        equal(S2xZip.Result.OK, S2xZip.extract(p, dest, null, new AtomicBoolean(false)));
        sameBytes(big, Files.readAllBytes(new File(dest, "data/99.data").toPath()), "resumed big file matches byte for byte");
        equal(MARGIN, S2xZip.spaceNeeded(p, dest));
    }

    // 8. 已存在的目标文件大小相同但 CRC 不对：续传必须重写，最后全对返回 OK
    // 8. an existing target with the same size but a bad CRC must be rewritten on resume; the final check returns OK
    static void badCrcExistingCase() throws IOException {
        File z = zip("badcrc.zip", x -> game(x, "", true));
        S2xZip.Plan p = S2xZip.plan(z);
        File dest = dir("badcrc-dest");
        write(new File(dest, "ExeFS/main"), "MAIN?");   // 与 "main!" 同为 5 字节，CRC 不同 | 5 bytes like "main!", different CRC
        equal(MAIN.length, (int) new File(dest, "ExeFS/main").length());
        equal(S2xZip.Result.OK, S2xZip.extract(p, dest, null, new AtomicBoolean(false)));
        equal("main!", text(new File(dest, "ExeFS/main")));
        // 再跑一次（全部命中 CRC）也应 OK，且内容不变 | a second run (all CRCs match) is OK too and leaves the content alone
        equal(S2xZip.Result.OK, S2xZip.extract(p, dest, null, new AtomicBoolean(false)));
        equal("main!", text(new File(dest, "ExeFS/main")));
    }

    // 9. zip64：70000 个空条目把中央目录推过 65535，plan 与 extract 都要正常
    // 9. zip64: 70000 empty entries push the central directory past 65535; plan and extract must both work
    static void zip64Case() throws IOException {
        File z = zip("zip64.zip", x -> {
            game(x, "S2X/", true);
            for (int i = 0; i < ZIP64_ENTRIES; i++) emptyEntry(x, String.format("S2X/pad/%05d/", i));
        });
        S2xZip.Plan p = S2xZip.plan(z);
        ok(p.entryCount > 65535, "central directory is zip64 (" + p.entryCount + " entries)");
        equal(4, p.files.size());
        ok(p.ok(), "zip64 pack is structurally fine");
        File dest = dir("zip64-dest");
        equal(S2xZip.Result.OK, S2xZip.extract(p, dest, null, new AtomicBoolean(false)));
        equal("main!", text(new File(dest, "ExeFS/main")));
        equal("config=1\n", text(new File(dest, "data/.build.config")));
    }
}
