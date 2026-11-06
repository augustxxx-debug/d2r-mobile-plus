package com.xiaoboluo.d2r;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

// 启动前「启动 / 导入 / 生成」判定：直接调用 StartupDecision.decide / contentReady（不是 Python 平行实现），在临时目录里造各种运行目录状态。
// The pre-start "boot / import / generate" decision: calls StartupDecision.decide / contentReady directly (not a Python parallel implementation), building every
// runtime-directory state in temporary directories.
//
// 覆盖：本版内容齐全、旧不配套导入包 + 齐全运行目录（P0-1）、配套导入包、缺六件程序任一件（M3）、缺 .xgs（M5）、缺 overlay（M2）、缺 derived.tsv、
// 版本记录缺失 / 不符、换上没做完（install.pending / import.part，N1 / N7）、内外部运行目录规则（N6）、生成中（N5）、产物代号解析（N2）、
// 导入在删 import.part 前被杀后的恢复（P2-1），以及生成熔断只数连续失败、成功 / 再试一次 / 升级版本清零（P2-4 返工）。
// Covers: ready content, an incompatible old import pack with ready runtime (P0-1), a matching import pack, each missing program file (M3), no .xgs (M5),
// no overlay (M2), no derived.tsv, missing / wrong version record, unfinished install (install.pending / import.part, N1 / N7), the internal/external
// runtime rule (N6), generation in progress (N5), product parsing (N2), recovery when the import was killed before deleting import.part (P2-1), and the
// generation breaker counting consecutive failures only, cleared by success / "Try again" / a new version (P2-4 rework).
public class StartupDecisionTest {
    static final String PRODUCT = "3.3.94012-r44";
    static int cases;
    static File root;

    static void equal(Object want, Object actual) {
        cases++;
        if (want == null ? actual != null : !want.equals(actual))
            throw new AssertionError("case " + cases + ": want=" + want + " actual=" + actual);
    }

    static void write(File f, String text) throws IOException {
        File p = f.getParentFile();
        if (p != null && !p.isDirectory() && !p.mkdirs()) throw new IOException("mkdir " + p);
        Files.write(f.toPath(), text.getBytes(StandardCharsets.UTF_8));
    }

    static File dir(String name) throws IOException {
        File d = new File(root, name);
        if (!d.isDirectory() && !d.mkdirs()) throw new IOException("mkdir " + d);
        return d;
    }

    // 能直接启动的运行目录 | a runtime directory that can boot directly
    static File runtime(String name) throws IOException {
        File r = dir(name);
        for (String m : StartupDecision.MODULES) write(new File(r, "out/" + m), "x");
        write(new File(r, "shaders/a.xgs"), "x");
        new File(r, "overlay").mkdirs();
        write(new File(r, "derived.tsv"), "table\n");
        write(new File(r, "pack_info.txt"), "PRODUCT=" + PRODUCT + "\n");
        return r;
    }

    static File source(String name, String product) throws IOException {
        File s = dir(name);
        write(new File(s, "pack_info.txt"), "PRODUCT=" + product + "\nMADE=old\n");
        return s;
    }

    static StartupDecision.Action decide(File src, File rt, File ext, String product, boolean generating) {
        return StartupDecision.decide(src, rt, ext, product, generating);
    }

    static void remove(File f) throws IOException { PackGenerator.deleteTree(f); }

    public static void main(String[] args) throws Exception {
        root = Files.createTempDirectory("dm-startup-decision").toFile();
        try {
            // 本版内容齐全 → 启动；反复判定答案不变 | ready content boots; repeated decisions agree
            File ready = runtime("ready/dm");
            equal(StartupDecision.Action.BOOT, decide(null, ready, null, PRODUCT, false));
            equal(StartupDecision.Action.BOOT, decide(null, ready, null, PRODUCT, false));

            // P0-1 / N3：下载目录里留着不配套的旧电脑导入包，运行目录已是本版内容 → 必须启动，绝不因为它生成
            // P0-1 / N3: an incompatible old computer pack in the download folder with this build's content already in place -> boot, never generate
            File old = source("Download/S2X/D2R Mobile/android", "3.3.94012-r43");
            equal(StartupDecision.Action.BOOT, decide(old, ready, null, PRODUCT, false));
            File stale = source("Download/stale/android", "stale-overlay");
            equal(StartupDecision.Action.BOOT, decide(stale, ready, null, PRODUCT, false));
            equal(StartupDecision.Action.BOOT, decide(old, ready, null, PRODUCT, false));

            // 空运行目录 + 旧包 → 只生成一次（M4 恒 BOOT 会在这里现形） | empty runtime + old pack -> generate (an always-BOOT M4 shows up here)
            File empty = dir("empty/dm");
            equal(StartupDecision.Action.GENERATE, decide(old, empty, null, PRODUCT, false));
            equal(StartupDecision.Action.GENERATE, decide(null, empty, null, PRODUCT, false));

            // 与本版配套、和已导入不同的导入包 → 导入；与已导入的逐字相同 → 启动 | a matching pack that differs from the imported one -> import; identical -> boot
            File matching = source("Download/matching/android", PRODUCT);
            equal(StartupDecision.Action.IMPORT, decide(matching, ready, null, PRODUCT, false));
            equal(StartupDecision.Action.IMPORT, decide(matching, empty, null, PRODUCT, false));
            File same = runtime("same/dm");
            write(new File(same, "pack_info.txt"), "PRODUCT=" + PRODUCT + "\nMADE=old\n");
            equal(StartupDecision.Action.BOOT, decide(matching, same, null, PRODUCT, false));

            // M3：六件程序缺任何一件都不能启动 | M3: missing any one of the six program files must not boot
            for (int i = 0; i < StartupDecision.MODULES.length; i++) {
                File r = runtime("modules/missing-" + i);
                remove(new File(r, "out/" + StartupDecision.MODULES[i]));
                equal(StartupDecision.Action.GENERATE, decide(null, r, null, PRODUCT, false));
                equal(false, StartupDecision.contentReady(r, PRODUCT));
            }

            // M5：没有 .xgs 不能启动；着色器目录整个缺失同样不能 | M5: no .xgs bundle must not boot; a missing shaders directory neither
            File noXgs = runtime("shaders/none");
            remove(new File(noXgs, "shaders/a.xgs"));
            equal(StartupDecision.Action.GENERATE, decide(null, noXgs, null, PRODUCT, false));
            equal(false, StartupDecision.contentReady(noXgs, PRODUCT));
            File noShaders = runtime("shaders/nodir");
            remove(new File(noShaders, "shaders"));
            equal(false, StartupDecision.contentReady(noShaders, PRODUCT));

            // M2：overlay 必须是目录 | M2: overlay must be a directory
            File noOverlay = runtime("overlay/none");
            remove(new File(noOverlay, "overlay"));
            equal(StartupDecision.Action.GENERATE, decide(null, noOverlay, null, PRODUCT, false));
            equal(false, StartupDecision.contentReady(noOverlay, PRODUCT));

            // P1-1：缺 derived.tsv 是半份内容 | P1-1: missing derived.tsv is half a pack
            File noDerived = runtime("derived/none");
            remove(new File(noDerived, "derived.tsv"));
            equal(StartupDecision.Action.GENERATE, decide(null, noDerived, null, PRODUCT, false));
            equal(false, StartupDecision.contentReady(noDerived, PRODUCT));

            // P1-1：缺版本记录 / 版本不符 / 没有期望版本都不能算齐全 | P1-1: no record / wrong record / no expected product is never ready
            File noRecord = runtime("record/none");
            remove(new File(noRecord, "pack_info.txt"));
            equal(StartupDecision.Action.GENERATE, decide(null, noRecord, null, PRODUCT, false));
            equal(false, StartupDecision.contentReady(noRecord, PRODUCT));
            File wrongRecord = runtime("record/wrong");
            write(new File(wrongRecord, "pack_info.txt"), "PRODUCT=3.3.94012-r43\n");
            equal(StartupDecision.Action.GENERATE, decide(null, wrongRecord, null, PRODUCT, false));
            equal(false, StartupDecision.contentReady(wrongRecord, PRODUCT));
            equal(false, StartupDecision.contentReady(ready, null));

            // P1-1 / N1 / N7：换上没做完（install.pending 或 import.part 残留）一律不算齐全
            // P1-1 / N1 / N7: an unfinished swap (install.pending or import.part left over) is never ready
            File pending = runtime("unfinished/pending");
            write(new File(pending, "install.pending"), "1");
            equal(StartupDecision.Action.GENERATE, decide(null, pending, null, PRODUCT, false));
            equal(false, StartupDecision.contentReady(pending, PRODUCT));
            File part = runtime("unfinished/part");
            write(new File(part, "import.part/pack_info.txt"), "PRODUCT=" + PRODUCT + "\n");
            equal(StartupDecision.Action.GENERATE, decide(null, part, null, PRODUCT, false));
            equal(false, StartupDecision.contentReady(part, PRODUCT));

            // 内部没有主程序时看外部私有目录；内部有主程序但不全时不能被外部盖过去（N6）；内部换上没做完也不能用外部顶上
            // External private storage counts when internal has no main module; an incomplete internal with main must not be masked by external (N6);
            // an unfinished internal swap must not be papered over with external content
            File ext = runtime("external/ext");
            File internalEmpty = dir("external/internal-empty");
            equal(StartupDecision.Action.BOOT, decide(null, internalEmpty, ext, PRODUCT, false));
            File internalMain = dir("external/internal-main");
            write(new File(internalMain, "out/main.patched.bin"), "m");
            equal(StartupDecision.Action.GENERATE, decide(null, internalMain, ext, PRODUCT, false));
            File internalPending = dir("external/internal-pending");
            write(new File(internalPending, "install.pending"), "1");
            equal(StartupDecision.Action.GENERATE, decide(null, internalPending, ext, PRODUCT, false));

            // N5：生成进行中就留在加载页（GENERATE），哪怕运行目录已经齐全 | N5: generation in progress keeps the loading page even when content is ready
            equal(StartupDecision.Action.GENERATE, decide(null, ready, null, PRODUCT, true));

            // N2：产物代号从 PRODUCT= 之后原样取（截错位会让每次生成完都判不齐）| N2: the product is taken verbatim after PRODUCT=
            equal(PRODUCT, StartupDecision.packProduct("A=1\nPRODUCT=" + PRODUCT + "\nB=2\n"));
            equal(null, StartupDecision.packProduct(null));
            equal(null, StartupDecision.packProduct("X=1\n"));

            // P2-1：导入在删 import.part 之前被杀（没有 install.pending），下载目录里还有本版配套包 → 重走导入即可恢复
            // P2-1: the import was killed before deleting import.part (no install.pending) and the download folder still has this build's pack -> redo the import
            File p21src = source("p2-1/download/android", PRODUCT);
            File p21 = runtime("p2-1/runtime");
            write(new File(p21, "pack_info.txt"), "PRODUCT=" + PRODUCT + "\nMADE=old\n");   // 导入拷过去的就是这份 | the import copies exactly this
            write(new File(p21, "import.part/pack_info.txt"), "PRODUCT=" + PRODUCT + "\n");
            equal(StartupDecision.Action.IMPORT, decide(p21src, p21, null, PRODUCT, false));
            // 有 install.pending（生成换上的标记）时不能只靠重导入：importPack 不会清它 → 重新生成
            // with install.pending (the generation-swap marker) a mere re-import would not clear it: importPack never removes it -> generate
            File both = runtime("p2-1/both");
            write(new File(both, "pack_info.txt"), "PRODUCT=" + PRODUCT + "\nMADE=old\n");
            write(new File(both, "import.part/x"), "x");
            write(new File(both, "install.pending"), "1");
            equal(StartupDecision.Action.GENERATE, decide(p21src, both, null, PRODUCT, false));
            // 下载目录没有配套包（删掉了 / 版本不符）就照旧生成 | no matching pack in the download folder (deleted / other version) -> generate as before
            File p21b = runtime("p2-1/runtime-no-source");
            write(new File(p21b, "import.part/x"), "x");
            equal(StartupDecision.Action.GENERATE, decide(null, p21b, null, PRODUCT, false));
            File p21old = source("p2-1/old-source", "3.3.94012-r43");
            equal(StartupDecision.Action.GENERATE, decide(p21old, p21b, null, PRODUCT, false));

            // P2-4 返工：熔断只数「连续失败」；生成成功 / 「再试一次」清零；升级版本自然重置
            // P2-4 rework: the breaker counts consecutive failures only; a ready build / "Try again" clears it; a new version resets it
            final long ver = 10203L;
            StartupDecision.GenOutcome READY = StartupDecision.GenOutcome.READY, FAILED = StartupDecision.GenOutcome.FAILED, RETRY = StartupDecision.GenOutcome.RETRY;
            // 成功后清零：之前连续失败 2 次，本轮内容齐全 → 计数归零、不熔断 | success clears: 2 failures before, this round is ready -> 0, no breaker
            equal(0, StartupDecision.genTriesNext(READY, ver, ver, 2));
            equal(false, StartupDecision.generationLimitReached(ver, ver, 0));
            // 连续失败两次熔断（上限仍是 2）| two consecutive failures trip the breaker (cap still 2)
            equal(1, StartupDecision.genTriesNext(FAILED, ver, ver, 0));
            equal(2, StartupDecision.genTriesNext(FAILED, ver, ver, 1));
            equal(true, StartupDecision.generationLimitReached(ver, ver, 2));
            equal(false, StartupDecision.generationLimitReached(ver, ver, 1));
            // 失败、成功、失败：中间成功过一次就重新计数，不熔断 | failure, success, failure: the success resets, so no breaker
            equal(1, StartupDecision.genTriesNext(FAILED, ver, ver, 0));
            equal(0, StartupDecision.genTriesNext(READY, ver, ver, 1));
            equal(1, StartupDecision.genTriesNext(FAILED, ver, ver, 0));
            equal(false, StartupDecision.generationLimitReached(ver, ver, 1));
            // 「再试一次」：清零并重新计数；再失败两次才重新熔断 | "Try again" clears and recounts; two fresh failures trip it again
            equal(0, StartupDecision.genTriesNext(RETRY, ver, ver, 2));
            equal(false, StartupDecision.generationLimitReached(ver, ver, 0));
            equal(1, StartupDecision.genTriesNext(FAILED, ver, ver, 0));
            equal(false, StartupDecision.generationLimitReached(ver, ver, 1));
            equal(2, StartupDecision.genTriesNext(FAILED, ver, ver, 1));
            equal(true, StartupDecision.generationLimitReached(ver, ver, 2));
            // 升级版本（versionCode 变了）：旧计数作废，不熔断；失败从 1 起，成功仍归零 | a new version discards the old count: no breaker; a failure starts at 1, a success is still 0
            final long newer = ver + 1;
            equal(false, StartupDecision.generationLimitReached(newer, ver, 2));
            equal(1, StartupDecision.genTriesNext(FAILED, newer, ver, 2));
            equal(0, StartupDecision.genTriesNext(READY, newer, ver, 2));
            equal(0, StartupDecision.genTriesNext(RETRY, newer, ver, 2));

            System.out.println("StartupDecision OK (" + cases + " cases)");
        } finally {
            PackGenerator.deleteTree(root);
        }
    }
}
