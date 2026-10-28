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
    private static void write(File f, String text) throws IOException { f.getParentFile().mkdirs(); Files.write(f.toPath(), text.getBytes(StandardCharsets.UTF_8)); }

    public static void main(String[] args) throws Exception {
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
        equal(false, s.accept("[21:33:58] 改写游戏程序 完成（22 秒）"));
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
        System.out.println("PackGenerator OK (" + cases + " cases)");
    }
}
