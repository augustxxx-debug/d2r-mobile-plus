package com.xiaoboluo.d2r;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Enumeration;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Set;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

// 玩家把 S2X 文件夹压成 zip 放进 Download，App 自己解（REQ28，B023）：电脑上「拷贝」会漏掉 data/.build.config 这类点开头的隐藏文件，
// 压缩包不会。这里只做不依赖 Android 的部分（javac 就能单测，见 tests/S2xZipTest.java），界面在下一张卡接。
// The player zips the S2X folder into Download and the App extracts it (REQ28, B023): a plain file copy on a computer silently drops dot files such as
// data/.build.config, a zip does not. Only the Android-free part lives here (unit-testable with plain javac, see tests/S2xZipTest.java); the UI comes next.
//
// 三步：find 在 Download 根目录找包；plan 只读中央目录给出结构结论（根前缀、必需项、总字节数、zip slip）；extract 解到
// PackGenerator.gameBase 选出的目录，支持续传与取消。解完逐文件核大小 + CRC32。
// Three steps: find locates the pack in the Download root; plan reads only the central directory and returns the structural verdict (root prefix,
// required items, total bytes, zip slip); extract unpacks into the directory chosen by PackGenerator.gameBase, resumable and cancellable. Every file is
// checked for size + CRC32 afterwards.
final class S2xZip {
    private S2xZip() {}

    // 下载根目录里认这三个名字（不分大小写，按此顺序取第一个） | the three names looked for in the Download root (case-insensitive, first in this order wins)
    static final String[] PACK_NAMES = {"S2X.zip", "D2R Mobile.zip", "D2R Mobile Plus.zip"};
    // 调用方拿总字节数与 getUsableSpace 比之前，先多留这么多余量 | headroom added before the caller compares the total with getUsableSpace
    static final long MARGIN = 200L << 20;
    static final int BUFFER = 64 << 10;
    static final long REPORT_BYTES = 1L << 20;   // 进度节流：每 1 MB 或每 200 ms 报一次 | progress throttle: every 1 MB or every 200 ms
    static final long REPORT_MS = 200;

    enum Result { OK, CANCELLED, FAILED }

    // 已写字节 / 总字节 / 当前文件名；节流由 extract 内部做 | bytes written / total / current file; extract itself throttles
    interface Progress {
        void onProgress(long done, long total, String current);
    }

    static final class Entry {
        final String zipName;   // 压缩包里的原名（含根前缀） | the original name in the zip (root prefix included)
        final String path;      // 解到 destBase 下的相对路径；data/build.config 归一成 data/.build.config | path under destBase; data/build.config is normalized to data/.build.config
        final long size;
        final long crc;

        private Entry(String zipName, String path, long size, long crc) {
            this.zipName = zipName;
            this.path = path;
            this.size = size;
            this.crc = crc;
        }
    }

    static final class Plan {
        final File zip;
        final String prefix;        // 外层多出的那一级（"S2X/"）或 ""（直接以 ExeFS/、data/ 开头） | the extra outer level ("S2X/") or "" (entries start with ExeFS/ or data/)
        final List<Entry> files;    // 要解的文件，已按目标路径去重 | files to extract, deduplicated by target path
        final List<String> missing; // 缺的必需项名字 | names of missing required items
        final String reject;        // zip slip 结论；非 null 表示整包拒绝 | zip-slip verdict; non-null rejects the whole pack
        final long totalBytes;
        final int entryCount;       // 中央目录条目数（zip64 测试用它确认包真的超过 65535 条） | central-directory entry count (the zip64 test uses it to confirm > 65535 entries)

        private Plan(File zip, String prefix, List<Entry> files, List<String> missing, String reject, long totalBytes, int entryCount) {
            this.zip = zip;
            this.prefix = prefix;
            this.files = files;
            this.missing = missing;
            this.reject = reject;
            this.totalBytes = totalBytes;
            this.entryCount = entryCount;
        }

        // 结构上能解：不是 zip slip，且必需项齐全 | structurally extractable: no zip slip and every required item present
        boolean ok() { return reject == null && missing.isEmpty(); }
    }

    // 在 Download 根目录找包：S2X.zip → D2R Mobile.zip → D2R Mobile Plus.zip，名字不分大小写；只看根目录，不递归
    // Find the pack in the Download root: S2X.zip -> D2R Mobile.zip -> D2R Mobile Plus.zip, case-insensitive; only the root directory, no recursion
    static File find(File downloads) {
        if (downloads == null) return null;
        String[] names = downloads.list();
        if (names == null) return null;
        Arrays.sort(names);
        for (String want : PACK_NAMES) {
            for (String name : names) {
                if (!name.equalsIgnoreCase(want)) continue;
                File f = new File(downloads, name);
                if (f.isFile()) return f;
            }
        }
        return null;
    }

    // 首启要不要走解压（REQ28）：游戏文件没放齐（missingInputs 非空）且 Download 根目录找到了包。
    // 游戏文件已经齐了完全不看 zip：现有启动 / 导入 / 生成判定原样走。纯判定，不碰文件；find 已保证找到的是普通文件。
    // Whether the first launch should unpack (REQ28): the game files are incomplete (missingInputs non-empty) and a pack was found in the
    // Download root. With complete game files the zip is not looked at at all: the existing boot / import / generate decision runs unchanged.
    // Pure judgement, touches no file; find already guarantees the result is a regular file.
    static boolean shouldOffer(List<String> missingInputs, File zipFound) {
        return missingInputs != null && !missingInputs.isEmpty() && zipFound != null;
    }

    // 只读中央目录：识别根前缀、必需项、总字节数、zip slip。不解压任何内容。
    // Read only the central directory: root prefix, required items, total bytes, zip slip. Nothing is decompressed.
    static Plan plan(File zip) throws IOException {
        if (zip == null || !zip.isFile()) throw new IOException("no zip: " + zip);
        try (ZipFile zf = new ZipFile(zip)) {
            List<ZipEntry> entries = new ArrayList<>();
            Enumeration<? extends ZipEntry> en = zf.entries();
            while (en.hasMoreElements()) entries.add(en.nextElement());

            String prefix = detectPrefix(entries);
            // JDK 17 的 ZipEntry 不暴露外部属性，所以另读一遍中央目录找出符号链接 | JDK 17's ZipEntry does not expose external attributes, so the central directory is read once more to find symlinks
            Set<String> links = symlinkNames(zip);
            LinkedHashMap<String, Entry> byPath = new LinkedHashMap<>();
            for (ZipEntry ze : entries) {
                String name = norm(ze.getName());
                // zip slip 在剥前缀之前查，任何一条都拒绝整包 | zip slip is checked before stripping the prefix; one bad entry rejects the pack
                String bad = unsafe(name, links.contains(name));
                if (bad != null)
                    return new Plan(zip, prefix, new ArrayList<Entry>(), new ArrayList<String>(), bad, 0, entries.size());
                if (ze.isDirectory() || name.endsWith("/")) continue;
                String rel = name;
                if (!prefix.isEmpty()) {
                    if (!rel.startsWith(prefix)) continue;   // 前缀之外的东西（__MACOSX、说明文件等）不解 | entries outside the root (__MACOSX, notes...) are not extracted
                    rel = rel.substring(prefix.length());
                }
                rel = stripDotSlash(rel);
                if (rel.isEmpty() || junk(rel)) continue;
                // 只解游戏文件的两棵树：包里别的东西（例如玩家放在同一文件夹的 android/ 导入包）不解、不覆盖（Opus 审 P2-1）
                // only the two game-file trees are extracted: anything else in the pack (e.g. an android/ import pack in the same folder) is neither extracted nor overwritten (Opus review P2-1)
                if (!rel.startsWith("ExeFS/") && !rel.startsWith("data/")) continue;
                // 与 PackGenerator 现有兼容规则一致：没有点的 build.config 也认，解出来改名成 .build.config
                // Same compatibility rule as PackGenerator: a build.config without the dot is accepted and renamed to .build.config when extracted
                String path = rel.equals("data/build.config") ? "data/.build.config" : rel;
                Entry prev = byPath.get(path);
                if (prev != null && norm(prev.zipName).endsWith("/.build.config")) continue;   // 两种写法都在时保留点开头的那份 | with both spellings keep the dot one
                // 存包里的原名：反斜杠 / 「./」开头的包（Windows PowerShell 打的）规整后的名字在 ZipFile.getEntry 里查不到（Opus 审 P1-1）
                // keep the raw name in the pack: for backslash / "./" packs (Windows PowerShell) the normalized name is not found by ZipFile.getEntry (Opus review P1-1)
                byPath.put(path, new Entry(ze.getName(), path, Math.max(0, ze.getSize()), ze.getCrc()));
            }

            List<Entry> files = new ArrayList<>(byPath.values());
            boolean main = false, sdk = false, config = false, data = false;
            for (Entry e : files) {
                if (e.path.equals("ExeFS/main")) main = true;
                else if (e.path.equals("ExeFS/sdk")) sdk = true;
                else if (e.path.equals("data/.build.config")) config = true;
                if (e.path.startsWith("data/") && e.path.endsWith(".data") && e.path.indexOf('/', 5) < 0) data = true;
            }
            List<String> missing = new ArrayList<>();
            if (!main) missing.add("ExeFS/main");
            if (!sdk) missing.add("ExeFS/sdk");
            if (!config) missing.add("data/.build.config");
            if (!data) missing.add("data/*.data");
            // 启动判定（PackGenerator.missingInputs）还要的文件也得在包里，否则解完仍缺、每次冷启动都要重验整包（Opus 审 P2-2）
            // files the startup check (PackGenerator.missingInputs) needs must be in the pack too, or every cold start re-checks the whole pack (Opus review P2-2)
            for (String need : PackGenerator.INPUTS) { boolean have = false; for (Entry e : files) if (e.path.equals(need)) { have = true; break; } if (!have && !missing.contains(need)) missing.add(need); }
            long total = 0;
            for (Entry e : files) total += e.size;
            return new Plan(zip, prefix, files, missing, null, total, entries.size());
        }
    }

    // 解到 destBase：每个文件先写 <名>.part 再改名。目标已存在且大小 + CRC32 都对就跳过（续传）；取消时返回 CANCELLED，
    // 已完成文件保留、当前 .part 删除；最后逐文件核大小 + CRC32，全对才 OK。
    // Extract into destBase: every file is written as <name>.part and then renamed. An existing target with the right size + CRC32 is skipped
    // (resume); on cancel CANCELLED is returned, finished files stay and the current .part is deleted; at the end every file is checked for
    // size + CRC32 and only a full match returns OK.
    static Result extract(Plan plan, File destBase, Progress cb, AtomicBoolean cancel) throws IOException {
        if (plan == null || plan.reject != null || !plan.missing.isEmpty() || destBase == null) return Result.FAILED;
        File base = destBase.getCanonicalFile();
        if (!base.isDirectory() && !base.mkdirs() && !base.isDirectory()) return Result.FAILED;
        Reporter rep = new Reporter(cb, plan.totalBytes);
        long done = 0;
        try (ZipFile zf = new ZipFile(plan.zip)) {
            for (Entry e : plan.files) {
                if (cancelled(cancel)) return Result.CANCELLED;
                File target = new File(base, e.path);
                if (!inside(base, target)) return Result.FAILED;   // 目标目录里若有符号链接指到外面，这里拦住 | a symlink in destBase pointing outside is caught here
                if (complete(target, e)) {
                    done += e.size;
                    rep.update(done, e.path, false);
                    continue;
                }
                File dir = target.getParentFile();
                if (dir != null && !dir.isDirectory() && !dir.mkdirs() && !dir.isDirectory()) return Result.FAILED;
                File part = new File(dir, target.getName() + ".part");
                ZipEntry ze = zf.getEntry(e.zipName);
                if (ze == null) return Result.FAILED;
                long n = 0;
                boolean stopped = false;
                CRC32 crc = new CRC32();
                try (InputStream in = zf.getInputStream(ze); OutputStream out = new FileOutputStream(part)) {
                    byte[] buf = new byte[BUFFER];
                    while (true) {
                        if (cancelled(cancel)) { stopped = true; break; }
                        int r = in.read(buf);
                        if (r < 0) break;
                        out.write(buf, 0, r);
                        crc.update(buf, 0, r);
                        n += r;
                        rep.update(done + n, e.path, false);
                    }
                } catch (IOException ex) {
                    delete(part);
                    throw ex;
                }
                if (stopped) { delete(part); return Result.CANCELLED; }
                if (n != e.size || crc.getValue() != e.crc) { delete(part); return Result.FAILED; }
                Files.move(part.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING);
                done += e.size;
                rep.update(done, e.path, false);
            }
        }
        // 逐文件复核（不是只看这次写的）：上次留下的、这次跳过的也要对 | verify every file (not just the ones written this run): skipped leftovers must match too
        for (Entry e : plan.files) {
            File target = new File(base, e.path);
            if (!target.isFile() || target.length() != e.size || crc32(target) != e.crc) return Result.FAILED;
        }
        rep.finish(plan.files.isEmpty() ? "" : plan.files.get(plan.files.size() - 1).path);
        return Result.OK;
    }

    // 还差多少空间：总字节减去已完成文件，再加 200 MB 余量 | space still needed: total minus finished files, plus 200 MB headroom
    static long spaceNeeded(Plan plan, File destBase) {
        long have = 0;
        if (plan != null && destBase != null)
            for (Entry e : plan.files) if (complete(new File(destBase, e.path), e)) have += e.size;
        long need = (plan == null ? 0 : plan.totalBytes) - have;
        return (need < 0 ? 0 : need) + MARGIN;
    }

    // ---- 内部 ---- | ---- internals ----

    // 外层多一级目录（S2X/、D2R Mobile Plus/）就剥掉；直接以 ExeFS/、data/ 开头就是 ""。先找直接开头的，再看下一级。
    // Strip one extra outer level (S2X/, D2R Mobile Plus/) when present; "" when entries start directly with ExeFS/ or data/. Direct markers win.
    // 根前缀：ExeFS/ 或 data/ 之前的那几级目录，最多两级（「S2X/」或照教程压整个 S2X 文件夹得到的「S2X/D2R Mobile Plus/」，Opus 审 P1-2）。
    // 各条目算出的前缀不一致就说不清，按没有前缀处理（结构检查会列出缺项）；直接以 ExeFS/、data/ 开头的优先。
    // Root prefix: the directory levels before ExeFS/ or data/, at most two ("S2X/", or "S2X/D2R Mobile Plus/" from zipping the whole S2X folder as the guide says,
    // Opus review P1-2). Entries disagreeing on the prefix are ambiguous and count as no prefix (the structure check lists what is missing); a pack starting directly
    // with ExeFS/ or data/ wins.
    static final int MAX_PREFIX_LEVELS = 2;
    private static String detectPrefix(List<ZipEntry> entries) {
        String found = null;
        for (ZipEntry ze : entries) {
            String p = prefixOf(norm(ze.getName()));
            if (p == null) continue;
            if (p.isEmpty()) return "";
            if (found == null) found = p;
            else if (!found.equals(p)) return "";
        }
        return found == null ? "" : found;
    }
    private static String prefixOf(String n) {
        int levels = 0, start = 0;
        while (true) {
            String rest = n.substring(start);
            if (rest.startsWith("ExeFS/") || rest.startsWith("data/")) return n.substring(0, start);
            int slash = n.indexOf('/', start);
            if (slash < 0 || slash == start || ++levels > MAX_PREFIX_LEVELS) return null;
            start = slash + 1;
        }
    }

    private static String unsafe(String name, boolean symlink) {
        if (name.isEmpty()) return "empty entry name";
        if (name.startsWith("/") || name.startsWith("\\")
                || (name.length() >= 2 && Character.isLetter(name.charAt(0)) && name.charAt(1) == ':'))
            return "absolute path: " + name;
        for (String part : name.split("/")) if (part.equals("..")) return "parent path: " + name;
        if (symlink) return "symbolic link: " + name;
        return null;
    }

    // 只读中央目录，找出外部属性里 S_IFMT == S_IFLNK 的条目；同时也看 Info-ZIP 的 ASi Unix 扩展（0x756E，里面也带 mode）。
    // Read only the central directory and collect entries whose external attributes say S_IFMT == S_IFLNK; Info-ZIP's ASi Unix extra field
    // (0x756E, which also carries the mode) is checked too.
    private static Set<String> symlinkNames(File zip) throws IOException {
        Set<String> out = new HashSet<>();
        try (RandomAccessFile raf = new RandomAccessFile(zip, "r")) {
            long len = raf.length();
            long tail = Math.min(len, 65557);
            byte[] buf = new byte[(int) tail];
            raf.seek(len - tail);
            raf.readFully(buf);
            int eocd = -1;
            for (int i = buf.length - 22; i >= 0; i--) if (u32(buf, i) == 0x06054b50L) { eocd = i; break; }
            if (eocd < 0) return out;
            long count = u16(buf, eocd + 10);
            long size = u32(buf, eocd + 12);
            long offset = u32(buf, eocd + 16);
            if (count == 0xFFFF || size == 0xFFFFFFFFL || offset == 0xFFFFFFFFL) {
                long eocdAbs = len - tail + eocd;
                if (eocdAbs >= 20) {
                    byte[] loc = new byte[20];
                    raf.seek(eocdAbs - 20);
                    raf.readFully(loc);
                    if (u32(loc, 0) == 0x07064b50L) {
                        long z64 = u64(loc, 8);
                        if (z64 >= 0 && z64 + 56 <= len) {
                            byte[] z = new byte[56];
                            raf.seek(z64);
                            raf.readFully(z);
                            if (u32(z, 0) == 0x06064b50L) {
                                count = u64(z, 32);
                                size = u64(z, 40);
                                offset = u64(z, 48);
                            }
                        }
                    }
                }
            }
            if (count <= 0 || size <= 0 || size > Integer.MAX_VALUE || offset < 0 || offset + size > len) return out;
            byte[] cd = new byte[(int) size];
            raf.seek(offset);
            raf.readFully(cd);
            int pos = 0;
            for (long i = 0; i < count && pos + 46 <= cd.length; i++) {
                if (u32(cd, pos) != 0x02014b50L) break;
                int nlen = u16(cd, pos + 28), elen = u16(cd, pos + 30), clen = u16(cd, pos + 32);
                int nameOff = pos + 46;
                if (nameOff + nlen + elen > cd.length) break;
                long attrs = u32(cd, pos + 38);
                boolean link = ((attrs >>> 16) & 0xF000) == 0xA000;
                if (!link) link = asiSymlink(cd, nameOff + nlen, elen);
                if (link) out.add(norm(new String(cd, nameOff, nlen, StandardCharsets.UTF_8)));
                pos += 46 + nlen + elen + clen;
            }
        }
        return out;
    }

    // Info-ZIP ASi Unix 扩展：version(1) + mode(2) + 链接目标 | Info-ZIP ASi Unix extra: version(1) + mode(2) + link target
    private static boolean asiSymlink(byte[] b, int off, int len) {
        int p = off, end = off + len;
        while (p + 4 <= end) {
            int id = u16(b, p), sz = u16(b, p + 2);
            if (p + 4 + sz > end) break;
            if (id == 0x756E && sz >= 3 && (u16(b, p + 5) & 0xF000) == 0xA000) return true;
            p += 4 + sz;
        }
        return false;
    }

    private static int u16(byte[] b, int o) { return (b[o] & 0xFF) | ((b[o + 1] & 0xFF) << 8); }

    private static long u32(byte[] b, int o) {
        return (b[o] & 0xFFL) | ((b[o + 1] & 0xFFL) << 8) | ((b[o + 2] & 0xFFL) << 16) | ((b[o + 3] & 0xFFL) << 24);
    }

    private static long u64(byte[] b, int o) { return u32(b, o) | (u32(b, o + 4) << 32); }

    private static String norm(String s) {
        String n = s == null ? "" : s.replace('\\', '/');
        return stripDotSlash(n);
    }

    private static String stripDotSlash(String s) {
        while (s.startsWith("./")) s = s.substring(2);
        return s;
    }

    private static boolean junk(String rel) {
        if (rel.startsWith("__MACOSX/")) return true;
        int slash = rel.lastIndexOf('/');
        return (slash < 0 ? rel : rel.substring(slash + 1)).equals(".DS_Store");
    }

    private static boolean cancelled(AtomicBoolean cancel) {
        return cancel != null && cancel.get();
    }

    private static boolean inside(File base, File target) {
        try {
            String b = base.getCanonicalPath();
            String t = target.getCanonicalPath();
            return t.equals(b) || t.startsWith(b + File.separator);
        } catch (IOException ex) {
            return false;
        }
    }

    // 目标文件已经完整（大小 + CRC32）：续传时跳过，spaceNeeded 也用它算已完成 | target is complete (size + CRC32): skipped on resume and counted by spaceNeeded
    private static boolean complete(File f, Entry e) {
        if (!f.isFile() || f.length() != e.size) return false;
        try {
            return crc32(f) == e.crc;
        } catch (IOException ex) {
            return false;
        }
    }

    private static long crc32(File f) throws IOException {
        CRC32 crc = new CRC32();
        byte[] buf = new byte[BUFFER];
        try (InputStream in = Files.newInputStream(f.toPath())) {
            int r;
            while ((r = in.read(buf)) > 0) crc.update(buf, 0, r);
        }
        return crc.getValue();
    }

    private static void delete(File f) {
        try { Files.deleteIfExists(f.toPath()); } catch (IOException ignored) { }
    }

    // 进度节流：每 1 MB 或每 200 ms 一次；结束强制报满 | progress throttle: every 1 MB or 200 ms; a forced full report at the end
    private static final class Reporter {
        private final Progress cb;
        private final long total;
        private long lastBytes;
        private long lastMs = System.currentTimeMillis();

        Reporter(Progress cb, long total) {
            this.cb = cb;
            this.total = total;
        }

        void update(long done, String name, boolean force) {
            if (cb == null) return;
            long now = System.currentTimeMillis();
            boolean due = force || done - lastBytes >= REPORT_BYTES
                    || (done != lastBytes && now - lastMs >= REPORT_MS);
            if (!due) return;
            lastBytes = done;
            lastMs = now;
            cb.onProgress(done, total, name);
        }

        void finish(String name) {
            if (cb != null) cb.onProgress(total, total, name);
        }
    }
}
