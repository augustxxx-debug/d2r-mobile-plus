package com.xiaoboluo.d2r;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.List;
import java.util.Map;

// 手机端生成导入包：缺什么游戏文件、进度标记与百分比、失败归类、生成结果换上（pack_info.txt 最后放）、子进程命令与环境。
// On-phone import pack generation: missing game files, progress markers and percent, failure classes, installing the result (pack_info.txt last), the child process command and environment.
public class PackGeneratorTest {
    private static int cases;
    private static void equal(Object want, Object actual) {
        cases++;
        if (!want.equals(actual)) throw new AssertionError("want=" + want + " actual=" + actual);
    }
    private static File temp() throws IOException { return Files.createTempDirectory("pgt").toFile(); }
    private static File made2() throws IOException {
        File m = temp();
        for (String item : new String[]{"out/main.patched.bin", "shaders/a.xgs", "overlay/overlay.manifest", "derived.tsv", "pack_info.txt"}) write(new File(m, item), item);
        return m;
    }
    private static void write(File f, String text) throws IOException { f.getParentFile().mkdirs(); Files.write(f.toPath(), text.getBytes(StandardCharsets.UTF_8)); }

    public static void main(String[] args) throws Exception {
        // 过期标记：只改 PRODUCT 行，其余原样；没有 PRODUCT 行就补一行 | stale marking: only the PRODUCT line changes; without one, one is added
        equal("A=1\nPRODUCT=stale-overlay\nB=2\n", PackGenerator.markStale("A=1\nPRODUCT=3.3.94012-r44\nB=2\n"));
        equal("PRODUCT=stale-overlay\nX=1", PackGenerator.markStale("X=1"));
        File base = temp();
        equal(Arrays.asList("ExeFS/main", "ExeFS/sdk", "data/build_manifest.psv"), PackGenerator.missingInputs(base));
        write(new File(base, "ExeFS/main"), "m"); write(new File(base, "data/build_manifest.psv"), "b");
        equal(Arrays.asList("ExeFS/sdk"), PackGenerator.missingInputs(base));
        write(new File(base, "ExeFS/sdk"), "s");
        equal(Arrays.asList(), PackGenerator.missingInputs(base));

        // 进度：标记 → 步骤；着色器按已出的个数在 36–97 之间走 | progress: marker -> step; shaders move between 36 and 97 by the count made
        PackGenerator.State s = new PackGenerator.State();
        equal(PackGenerator.Step.PREPARE, s.step); equal(0, s.percent());
        equal(true, s.accept("@progress product 3.3.94012-r44"));
        equal(true, s.accept("@progress patch")); equal(PackGenerator.Step.PATCH, s.step); equal(3, s.percent());
        equal(false, s.accept("[21:33:58] 处理游戏文件 完成（22 秒）"));
        s.accept("@progress shaders /data/x/shaders"); equal(PackGenerator.Step.SHADERS, s.step); equal("/data/x/shaders", s.shaderDir);
        equal(36, s.percent()); s.shadersMade = 6931; equal(97, s.percent()); s.shadersMade = 9999; equal(97, s.percent());
        s.shadersMade = 3465; equal(66, s.percent());
        s.accept("@progress finish"); equal(97, s.percent());
        equal(false, s.accept("@progress nonsense")); equal(PackGenerator.Step.FINISH, s.step);

        // 失败归类 | failure classes
        equal(PackGenerator.Failure.NONE, PackGenerator.classify(0, List.of("[22:00:57] 好了")));
        List<String> missing = List.of("[x] 还缺 2 项：", "  - 游戏文件 ExeFS/sdk（在 /x 下没找到）", "  - 游戏文件 RomFS/data/build_manifest.psv（在 /x 下没找到）");
        equal(PackGenerator.Failure.NEED_FILES, PackGenerator.classify(1, missing));
        equal("ExeFS/sdk, RomFS/data/build_manifest.psv", PackGenerator.missingDetail(missing));
        equal(PackGenerator.Failure.NO_SPACE, PackGenerator.classify(1, List.of("OSError: [Errno 28] No space left on device: '/data/x'")));
        equal(PackGenerator.Failure.STEP_FAILED, PackGenerator.classify(1, List.of("[x] 改写 main 没成功（退出码 1）：['boom']；完整输出 /x/main.patch.log")));
        equal(PackGenerator.Failure.INTERRUPTED, PackGenerator.classify(137, List.of("[21:59:21] 翻译全部着色器（约 2 分钟）")));
        equal(PackGenerator.Failure.STEP_FAILED, PackGenerator.classify(1, List.of("Traceback (most recent call last):", "ValueError: x")));

        // 换上：各项先进 import.part，最后才放 pack_info.txt；旧的同名项被替换 | install: items go through import.part, pack_info.txt last; old items are replaced
        File made = temp(), runtime = temp();
        for (String item : new String[]{"out/main.patched.bin", "shaders/a.xgs", "overlay/overlay.manifest", "derived.tsv"}) write(new File(made, item), "new " + item);
        write(new File(made, "pack_info.txt"), "PRODUCT=p\n");
        write(new File(runtime, "shaders/old.xgs"), "old");
        PackGenerator.install(made, runtime);
        equal("new shaders/a.xgs", new String(Files.readAllBytes(new File(runtime, "shaders/a.xgs").toPath()), StandardCharsets.UTF_8));
        equal(false, new File(runtime, "shaders/old.xgs").exists());
        equal("PRODUCT=p\n", new String(Files.readAllBytes(new File(runtime, "pack_info.txt").toPath()), StandardCharsets.UTF_8));
        equal(false, new File(runtime, "import.part").exists());
        equal(false, PackGenerator.installPending(runtime));
        equal(false, PackGenerator.pushedByDeveloper("PRODUCT=p\n", runtime, null));
        // 四审 A4-04：换上中途被杀（out/ 已就位、pack_info.txt 未到、标记还在）不能被当成开发推送；标记或 import.part 残留都算没做完
        // fourth audit A4-04: a swap killed midway (out/ in place, no pack_info.txt yet, marker still there) must not count as a developer push; a leftover marker or import.part means unfinished
        File killed = temp(); write(new File(killed, "out/main.patched.bin"), "m"); write(new File(killed, PackGenerator.PENDING), "1");
        equal(true, PackGenerator.installPending(killed));
        equal(false, PackGenerator.pushedByDeveloper(null, killed, null));
        File leftover = temp(); write(new File(leftover, "out/main.patched.bin"), "m"); write(new File(leftover, "import.part/pack_info.txt"), "PRODUCT=p\n");
        equal(false, PackGenerator.pushedByDeveloper(null, leftover, null));
        File dev = temp(); write(new File(dev, "out/main.patched.bin"), "m");
        equal(true, PackGenerator.pushedByDeveloper(null, dev, null));
        File devExt = temp(); write(new File(devExt, "out/main.patched.bin"), "m");
        equal(true, PackGenerator.pushedByDeveloper(null, temp(), devExt));
        PackGenerator.install(made2(), killed);   // 重新生成后换上成功，标记清掉 | a regenerated pack installs and clears the marker
        equal(false, PackGenerator.installPending(killed));
        // 四审 A4-09：gen_log.txt 写出前去掉应用目录、外部存储、APK 目录与包名 | fourth audit A4-09: app directory, external storage, APK directory and package name removed
        String raw = "rt /data/user/0/com.xiaoboluo.d2r/files/dmgen/rt\nexe /data/app/~~Ab12==/com.xiaoboluo.d2r-Cd34==/lib/arm64/libdmgen_py.so\n"
                + "in /storage/emulated/0/Download/S2X/D2R Mobile/ExeFS/main\nold /data/data/com.xiaoboluo.d2r/files\n/sdcard/Download/x\npkg com.xiaoboluo.d2r\nfont=f4b8a34d1f83 step ok";
        String clean = PackGenerator.scrubLog(raw, "/data/app/~~Ab12==/com.xiaoboluo.d2r-Cd34==", "/data/user/0/com.xiaoboluo.d2r", "com.xiaoboluo.d2r");
        equal("rt ~/files/dmgen/rt\nexe <apk>/lib/arm64/libdmgen_py.so\nin ~/Download/S2X/D2R Mobile/ExeFS/main\nold ~/files\n~/Download/x\npkg <app-id>\nfont=f4b8a34d1f83 step ok", clean);
        equal(false, clean.contains("/data/") || clean.contains("/storage/") || clean.contains("xiaoboluo"));
        // 五审 A5-04：工作资料 / 多用户的共享存储用户号不是 0 | fifth audit A5-04: shared storage of work profiles / other users is not 0
        equal("in ~/Download/a\nkeep /storage/emulated/legacy", PackGenerator.scrubLog("in /storage/emulated/10/Download/a\nkeep /storage/emulated/legacy", null, null, "com.xiaoboluo.d2r"));
        // 加载页阶段：七步各占一个序号、按顺序；权重七个、都为正、合计落在两台真机首启之间（391–446 秒）| loading-page stages: one index per step, in order; seven positive weights summing between the two phones' first launches
        equal(7, PackGenerator.Step.values().length); equal(7, PackGenerator.LOAD_WEIGHTS_MS.length);
        { int sum = 0; for (int i = 0; i < 7; i++) { equal(i, PackGenerator.loadStage(PackGenerator.Step.values()[i])); equal(true, PackGenerator.LOAD_WEIGHTS_MS[i] > 0); sum += PackGenerator.LOAD_WEIGHTS_MS[i]; }
          equal(true, sum >= 391000 && sum <= 446000); }
        File broken = temp(); write(new File(broken, "out/x"), "x");
        try { PackGenerator.install(broken, temp()); throw new AssertionError("install of an incomplete pack must fail"); }
        catch (IOException expected) { cases++; }

        // 清理生成目录不顺着符号链接删到玩家放的游戏文件 | cleaning the generation directory never follows a symlink into the player's game files
        File player = temp(), gen0 = temp();
        write(new File(player, "data/keep.data"), "game");
        new File(gen0, "game/RomFS").mkdirs();
        Files.createSymbolicLink(new File(gen0, "game/RomFS/data").toPath(), new File(player, "data").toPath());
        PackGenerator.deleteTree(gen0);
        equal(false, gen0.exists()); equal(true, new File(player, "data/keep.data").isFile());

        // 子进程：启动器在 nativeLibraryDir，翻译工具等从那里来；运行时与缓存在 files/dmgen | child process: launcher in nativeLibraryDir, tools from there; runtime and caches in files/dmgen
        File lib = new File("/data/app/x/lib/arm64"), gen = new File("/data/user/0/p/files/dmgen");
        List<String> cmd = PackGenerator.command(lib, gen, new File("/game"), new File("/out"));
        equal(Arrays.asList("/data/app/x/lib/arm64/libdmgen_py.so", "-X", "utf8", "/data/user/0/p/files/dmgen/src/dm/tools/d2rm_android.py", "/game", "/out"), cmd);
        Map<String, String> env = PackGenerator.environment(lib, gen, 8);
        equal("/data/app/x/lib/arm64/libdmgen_xlate.so", env.get("DMSH_XLATE"));
        equal("/data/app/x/lib/arm64/libdmgen_spvopt.so", env.get("SPIRV_OPT"));
        equal("/data/app/x/lib/arm64/libdmgen_spvval.so", env.get("SPIRV_VAL"));
        equal("/data/app/x/lib/arm64/libdmdeswizzle.so", env.get("DM_DESWIZZLE_LIB"));
        equal("/data/user/0/p/files/dmgen/rt/py", env.get("PYTHONHOME"));
        equal("/data/user/0/p/files/dmgen/rt/lib", env.get("LD_LIBRARY_PATH"));
        equal("/data/user/0/p/files/dmgen/root", env.get("DM_ROOT"));
        equal("/data/user/0/p/files/dmgen/queries", env.get("DM_PREBUILT_QUERIES"));
        equal("1", env.get("DM_GEN_PROGRESS")); equal("8", env.get("J"));
        equal(false, env.containsKey("DM_UI_FONT"));
        // 字体（APK 自带开源楷体时 DmActivity 解到 dmgen/font/）：font.txt 记文件名与加粗档 → DM_UI_FONT / DM_UI_FONT_WEIGHT；
        // 文件名只认同目录里的 .ttf、加粗档只认 0..8，不合规就不给（原版字）
        // Font (DmActivity unpacks the bundled open-licence Kai font to dmgen/font/): font.txt names the file and stroke level -> DM_UI_FONT / DM_UI_FONT_WEIGHT;
        // only a .ttf in the same folder and levels 0..8 are accepted, anything else gives no font (the original typeface)
        File genF = Files.createTempDirectory("dmgen-font").toFile();
        write(new File(genF, "font/font.txt"), "file=LXGWWenKai-Medium.ttf\nweight=0\nsha256=f4b8\n");
        Map<String, String> envF = PackGenerator.environment(lib, genF, 8);
        equal(new File(genF, "font/LXGWWenKai-Medium.ttf").getPath(), envF.get("DM_UI_FONT")); equal("0", envF.get("DM_UI_FONT_WEIGHT"));
        write(new File(genF, "font/font.txt"), "file=../outside.ttf\nweight=0\n");
        equal(false, PackGenerator.environment(lib, genF, 8).containsKey("DM_UI_FONT"));
        write(new File(genF, "font/font.txt"), "file=LXGWWenKai-Medium.ttf\nweight=9\n");
        equal(false, PackGenerator.environment(lib, genF, 8).containsKey("DM_UI_FONT"));
        PackGenerator.deleteTree(genF);

        // 失败日志（缺陷台账 B026）：@summary / @fail / 产物代号的解析；生成器给了类别就按它归类，没给照旧按输出猜
        // Failure log (defect ledger B026): parsing @summary / @fail / the product code; classify by the generator's kind when given, otherwise guess from the output as before
        PackGenerator.State st = new PackGenerator.State();
        equal(true, st.accept("@progress product 3.3.94012-r44")); equal("3.3.94012-r44", st.product);
        equal(true, st.accept("@summary 游戏文件核对：缺 data/.build.config（隐藏文件，要一起拷）"));
        equal(true, st.accept("@fail files")); equal(true, st.accept("@fail internal")); equal("files", st.failKind);   // 先到的算 | the first one counts
        equal(List.of("游戏文件核对：缺 data/.build.config（隐藏文件，要一起拷）"), st.summary);
        st.reset(); equal(0, st.summary.size()); equal(true, st.failKind == null && st.product == null);
        equal(PackGenerator.Failure.BAD_FILES, PackGenerator.classify(1, List.of("[x] 游戏文件不完整：…"), "files"));
        equal(PackGenerator.Failure.VERSION, PackGenerator.classify(1, List.of(), "version"));
        equal(PackGenerator.Failure.NO_SPACE, PackGenerator.classify(1, List.of(), "space"));
        equal(PackGenerator.Failure.STEP_FAILED, PackGenerator.classify(1, List.of(), "internal"));
        equal(PackGenerator.Failure.INTERRUPTED, PackGenerator.classify(-9, List.of(), "internal"));   // 被杀不信标记 | killed: the marker is not trusted
        equal(PackGenerator.Failure.NONE, PackGenerator.classify(0, List.of(), "files"));
        // 输入文件清单：每个文件的字节数，单独说 .build.config 在不在 | input file list: bytes of every file, and whether .build.config is there
        File inBase = temp(); write(new File(inBase, "ExeFS/main"), "mm"); write(new File(inBase, "data/00-00000004.data"), "dddd");
        String in = PackGenerator.listInputs(new File(inBase, "ExeFS"), new File(inBase, "data"));
        equal(true, in.contains("ExeFS：1 个\n  main\t2\n")); equal(true, in.contains("  00-00000004.data\t4\n"));
        equal(true, in.contains("data/.build.config：没有（隐藏文件，要一起拷）"));
        write(new File(inBase, "data/.build.config"), "cfg");
        equal(true, PackGenerator.listInputs(new File(inBase, "ExeFS"), new File(inBase, "data")).contains("data/.build.config：有"));
        equal(true, PackGenerator.listInputs(new File(inBase, "nope"), new File(inBase, "data")).contains("ExeFS：（没有这个文件夹）"));
        // gen_log：结论在第一屏，再环境、输入文件、生成输出 | gen_log: the verdict on the first screen, then environment, input files, generator output
        String g = PackGenerator.genLog("游戏文件不完整", "请重新拷", List.of("游戏文件核对：缺 data/.build.config"), "机型：X", in, "[12:00] 生成覆盖层\n");
        equal(true, g.startsWith("==== 结论 ====\n游戏文件不完整\n· 游戏文件核对：缺 data/.build.config\n请重新拷\n"));
        equal(true, g.indexOf("==== 环境 ====") < g.indexOf("==== 输入文件 ====") && g.indexOf("==== 输入文件 ====") < g.indexOf("==== 生成输出 ===="));
        // 分享附的各步骤日志与打包 | the per-step logs attached when sharing, and the packing
        File logGen = temp(); write(new File(logGen, "gen.log"), "main"); write(new File(logGen, "root/build/pack/android/gen_overlay.log"), "ov");
        write(new File(logGen, "root/build/pack/android/note.txt"), "x"); write(new File(logGen, "root/build/cache/dm/out/3.3.94012-r44-android/main.patch.log"), "p");
        List<File> logs = PackGenerator.stepLogs(logGen);
        equal(Arrays.asList("gen.log", "gen_overlay.log", "main.patch.log"), logs.stream().map(File::getName).collect(java.util.stream.Collectors.toList()));
        File zip = new File(temp(), "share/x.zip"); Map<String, byte[]> ent = new java.util.LinkedHashMap<>();
        ent.put("gen_log.txt", "a".getBytes(StandardCharsets.UTF_8)); ent.put("logs/android_gen_overlay.log", "ov".getBytes(StandardCharsets.UTF_8));
        PackGenerator.writeZip(zip, ent);
        try (java.util.zip.ZipFile z = new java.util.zip.ZipFile(zip)) {
            equal(Arrays.asList("gen_log.txt", "logs/android_gen_overlay.log"), z.stream().map(java.util.zip.ZipEntry::getName).collect(java.util.stream.Collectors.toList()));
        }
        PackGenerator.deleteTree(inBase); PackGenerator.deleteTree(logGen); PackGenerator.deleteTree(zip.getParentFile().getParentFile());
        System.out.println("PackGenerator OK (" + cases + " cases)");
    }
}
