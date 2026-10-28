package com.xiaoboluo.d2r;

import java.io.BufferedReader;
import java.io.File;
import java.nio.file.Files;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Deque;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

// 手机端生成导入包（不用电脑）：玩家把 Switch 版的 ExeFS（main、sdk）放到 Download/S2X/D2R Mobile/ExeFS、游戏资源照旧放 data，
// App 发现还没有导入包就自己跑 dm/tools/d2rm_android.py（与电脑上同一份脚本，App 自带 Python 3.13、NativeAOT 翻译工具、SPIRV-Tools，见 dm/android/tools/gen_runtime.sh），
// 产物与电脑上 make_pack.sh 做的逐字节相同，做好后像导入包一样换进 files/dm/。这里只放与安卓界面无关的部分（javac 就能单测，见 tests/PackGeneratorTest.java）。
// On-phone import pack generation (no computer): the player puts the Switch ExeFS (main, sdk) in Download/S2X/D2R Mobile/ExeFS and the game resources in data as before;
// when the App finds no import pack it runs dm/tools/d2rm_android.py itself (the same script as on a computer; the App carries Python 3.13, the NativeAOT translator and SPIRV-Tools, see
// dm/android/tools/gen_runtime.sh). The output is byte-identical to make_pack.sh on a computer and is swapped into files/dm/ like an import pack. Only the parts unrelated to the Android UI live
// here (unit-testable with plain javac, see tests/PackGeneratorTest.java).
final class PackGenerator {
    private PackGenerator() {}

    static final String[] INPUTS = {"ExeFS/main", "ExeFS/sdk", "data/build_manifest.psv"};
    static final String[] ITEMS = {"out", "shaders", "overlay", "derived.tsv", "pack_info.txt"};
    static final int SHADERS = 6931;   // 6,927 个游戏着色器 + 4 个查询核（dmsh_vk.py + pack_builtin_queries.py） | 6,927 game shaders + 4 query kernels
    static final long SPACE_NEEDED = 3L << 30;   // 缓存约 0.3 GB + 中间产物 0.7 GB + 成品 0.7 GB，留余量 | caches ~0.3 GB + intermediates 0.7 GB + result 0.7 GB, with headroom

    // 玩家放游戏文件的地方（Download/S2X/D2R Mobile）缺哪几样 | which game files are missing where the player puts them (Download/S2X/D2R Mobile)
    static List<String> missingInputs(File base) {
        List<String> missing = new ArrayList<>();
        for (String f : INPUTS) if (!new File(base, f).isFile()) missing.add(f);
        return missing;
    }

    // 步骤与各自占的进度区间（按奥丁上的耗时大致分） | steps and their progress ranges (roughly by the time each takes on the Thor)
    enum Step {
        PREPARE(0, 3), PATCH(3, 10), ASSETS(10, 18), OVERLAY(18, 30), PACKS(30, 36), SHADERS(36, 97), FINISH(97, 100);
        final int from, to;
        Step(int from, int to) { this.from = from; this.to = to; }
    }

    static final class State {
        volatile Step step = Step.PREPARE;
        volatile String shaderDir;
        volatile int shadersMade;

        // d2rm_android.py 在 DM_GEN_PROGRESS=1 时打「@progress <步骤> [参数]」；认得的返回 true | d2rm_android.py prints "@progress <step> [argument]" with DM_GEN_PROGRESS=1; true when recognised
        boolean accept(String line) {
            if (!line.startsWith("@progress ")) return false;
            String[] w = line.substring(10).split(" ", 2);
            switch (w[0]) {
                case "product": return true;
                case "patch": step = Step.PATCH; return true;
                case "assets": step = Step.ASSETS; return true;
                case "overlay": step = Step.OVERLAY; return true;
                case "packs": step = Step.PACKS; return true;
                case "shaders": step = Step.SHADERS; shaderDir = w.length > 1 ? w[1] : null; return true;
                case "finish": step = Step.FINISH; return true;
                default: return false;
            }
        }

        int percent() {
            Step s = step;
            if (s != Step.SHADERS) return s.from;
            return s.from + (s.to - s.from) * Math.min(shadersMade, SHADERS) / SHADERS;
        }
    }

    enum Failure { NONE, NEED_FILES, NO_SPACE, STEP_FAILED, INTERRUPTED }

    // 按退出码与输出末尾几行归类（d2rm_android.py 出错一律「[x] …」退出码 1；被系统杀掉是信号退出码） | classify by exit code and the last output lines (d2rm_android.py always fails with "[x] ..." and exit code 1; being killed by the system gives a signal exit code)
    static Failure classify(int exit, List<String> tail) {
        if (exit == 0) return Failure.NONE;
        for (String l : tail) if (l.contains("No space left on device") || l.contains("[Errno 28]")) return Failure.NO_SPACE;
        if (!missingDetail(tail).isEmpty()) return Failure.NEED_FILES;
        if (exit == 1) return Failure.STEP_FAILED;
        return Failure.INTERRUPTED;
    }

    // 「还缺 N 项」里的游戏文件（其余缺项是 App 自带工具的问题，按步骤失败报） | the game files in "还缺 N 项" (other missing items are problems with the App's own tools and are reported as a failed step)
    static String missingDetail(List<String> tail) {
        List<String> files = new ArrayList<>();
        for (String l : tail) {
            int i = l.indexOf("  - 游戏文件 ");
            if (i < 0) continue;
            String f = l.substring(i + 9); int j = f.indexOf('（');
            files.add(j < 0 ? f.trim() : f.substring(0, j).trim());
        }
        return String.join(", ", files);
    }

    // gen = files/dmgen：rt/（runtime.zip 解开）、src/（src.zip 解开）、queries/、root/（DM_ROOT：缓存与中间产物）
    // gen = files/dmgen: rt/ (runtime.zip unpacked), src/ (src.zip unpacked), queries/, root/ (DM_ROOT: caches and intermediates)
    static List<String> command(File nativeDir, File gen, File game, File out) {
        return Arrays.asList(new File(nativeDir, "libdmgen_py.so").getPath(), "-X", "utf8", new File(gen, "src/dm/tools/d2rm_android.py").getPath(), game.getPath(), out.getPath());
    }

    static Map<String, String> environment(File nativeDir, File gen, int jobs) {
        Map<String, String> e = new LinkedHashMap<>();
        File root = new File(gen, "root");
        e.put("PYTHONHOME", new File(gen, "rt/py").getPath());
        e.put("LD_LIBRARY_PATH", new File(gen, "rt/lib").getPath());
        e.put("PYTHONDONTWRITEBYTECODE", "1");
        e.put("HOME", gen.getPath());
        e.put("TMPDIR", new File(root, "tmp").getPath());
        e.put("DM_ROOT", root.getPath());
        e.put("DM_GEN_ASSETS", new File(root, "gen-assets").getPath());
        e.put("DMSH_XLATE", new File(nativeDir, "libdmgen_xlate.so").getPath());
        e.put("SPIRV_OPT", new File(nativeDir, "libdmgen_spvopt.so").getPath());
        e.put("SPIRV_VAL", new File(nativeDir, "libdmgen_spvval.so").getPath());
        e.put("DM_DESWIZZLE_LIB", new File(nativeDir, "libdmdeswizzle.so").getPath());
        e.put("DM_PREBUILT_QUERIES", new File(gen, "queries").getPath());
        e.put("DOTNET_SYSTEM_GLOBALIZATION_INVARIANT", "1");
        e.put("DM_GEN_PROGRESS", "1");
        e.put("J", Integer.toString(jobs));
        // 开源楷体（APK 自带时 DmActivity 解到 dmgen/font/）：font.txt 记文件名与加粗档；只认同目录里的 .ttf 与 0..8 档，不合规就不给（原版字）
        // The bundled open-licence Kai font (unpacked by DmActivity to dmgen/font/): font.txt names the file and the stroke level; only a .ttf in that folder and levels 0..8, otherwise no font (original typeface)
        File fontDir = new File(gen, "font"), spec = new File(fontDir, "font.txt");
        if (spec.isFile()) {
            String file = null, weight = "0";
            try { for (String line : Files.readAllLines(spec.toPath(), StandardCharsets.UTF_8)) { if (line.startsWith("file=")) file = line.substring(5).trim(); else if (line.startsWith("weight=")) weight = line.substring(7).trim(); } }
            catch (IOException ignored) { file = null; }
            if (file != null && file.matches("[A-Za-z0-9._-]+\\.ttf") && !file.startsWith(".") && weight.matches("[0-8]")) { e.put("DM_UI_FONT", new File(fontDir, file).getPath()); e.put("DM_UI_FONT_WEIGHT", weight); }
        }
        return e;
    }

    // 跑生成脚本：逐行记日志、喂给进度；返回退出码，最后 40 行留在 tail 里 | run the generator script: log each line and feed progress; returns the exit code, the last 40 lines are kept in tail
    static int run(List<String> cmd, Map<String, String> env, File cwd, File log, State state, Deque<String> tail) throws IOException, InterruptedException {
        ProcessBuilder pb = new ProcessBuilder(cmd).directory(cwd).redirectErrorStream(true);
        pb.environment().putAll(env);
        new File(env.get("TMPDIR")).mkdirs();
        Process p = pb.start();
        try (BufferedReader r = new BufferedReader(new InputStreamReader(p.getInputStream(), StandardCharsets.UTF_8));
             OutputStream out = new FileOutputStream(log)) {
            for (String line; (line = r.readLine()) != null; ) {
                out.write((line + "\n").getBytes(StandardCharsets.UTF_8)); out.flush();
                state.accept(line);
                tail.addLast(line); while (tail.size() > 40) tail.removeFirst();
            }
        } finally {
            if (Thread.interrupted()) p.destroyForcibly();
        }
        return p.waitFor();
    }

    static Deque<String> newTail() { return new ArrayDeque<>(); }

    // 解开 zip（只许落在 dest 里） | unpack a zip (entries must stay inside dest)
    static void unzip(InputStream in, File dest) throws IOException {
        String root = dest.getCanonicalPath() + File.separator;
        byte[] buffer = new byte[1 << 16];
        try (ZipInputStream z = new ZipInputStream(in)) {
            for (ZipEntry e; (e = z.getNextEntry()) != null; ) {
                File f = new File(dest, e.getName());
                if (!f.getCanonicalPath().startsWith(root)) throw new IOException("zip entry outside: " + e.getName());
                if (e.isDirectory()) { f.mkdirs(); continue; }
                File parent = f.getParentFile();
                if (parent != null && !parent.isDirectory() && !parent.mkdirs()) throw new IOException("mkdir " + parent);
                try (OutputStream o = new FileOutputStream(f)) { for (int n; (n = z.read(buffer)) > 0; ) o.write(buffer, 0, n); }
            }
        }
    }

    // 生成结果换进运行目录：与导入同一规矩——先进 import.part，齐了逐项换上，pack_info.txt 最后放；同一分区里只改名不复制
    // swap the result into the runtime directory, by the same rule as importing: into import.part first, then item by item, pack_info.txt last; within one partition only renames, no copying
    static void install(File made, File runtime) throws IOException {
        for (String item : ITEMS) if (!new File(made, item).exists()) throw new IOException("missing " + item);
        File part = new File(runtime, "import.part");
        deleteTree(part);
        if (!part.mkdirs()) throw new IOException("mkdir import.part");
        for (String item : ITEMS) if (!new File(made, item).renameTo(new File(part, item))) throw new IOException("rename " + item);
        for (String item : ITEMS) {
            File from = new File(part, item), to = new File(runtime, item);
            deleteTree(to);
            if (!from.renameTo(to)) throw new IOException("rename " + item);
        }
        deleteTree(part);
    }

    static void deleteTree(File f) throws IOException {
        File[] list = f.isDirectory() && !isLink(f) ? f.listFiles() : null;
        if (list != null) for (File c : list) deleteTree(c);
        if ((f.exists() || isLink(f)) && !f.delete()) throw new IOException("delete " + f.getName());
    }

    private static boolean isLink(File f) { return java.nio.file.Files.isSymbolicLink(f.toPath()); }

    static int count(File dir, String suffix) {
        String[] names = dir == null ? null : dir.list();
        int n = 0;
        if (names != null) for (String s : names) if (s.endsWith(suffix)) n++;
        return n;
    }
}
