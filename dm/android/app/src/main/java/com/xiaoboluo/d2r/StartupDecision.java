package com.xiaoboluo.d2r;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

// 原生库加载之前的纯判定：外部导入包 + 运行目录的实际内容 → 启动 / 导入 / 生成。不依赖 Android，javac 就能单测。
// The pure decision before the native library loads: the external import pack + the actual runtime-directory content -> boot / import / generate.
// No Android dependencies, so plain javac can unit-test it.
//
// 三条规矩（B134 返工）| three rules (B134 rework):
// 1. 下载目录里与本版不配套的旧电脑导入包当作「没有」，绝不再因为它触发生成；只看运行目录本身（P0-1）。
//    An old computer-made import pack in the download folder that does not match this build counts as "absent" and never triggers generation by
//    itself; only the runtime directory is judged (P0-1).
// 2. 运行目录的内容要能真正启动才算齐全：六件程序、至少一个 .xgs、overlay 目录、derived.tsv，以及本版的 pack_info.txt 版本记录；
//    上次换上没做完（install.pending / import.part 残留）一律不算（P1-1，四审 A4-04）。
//    Runtime content only counts when it can really boot: the six program files, at least one .xgs, the overlay directory, derived.tsv and a
//    pack_info.txt version record for this build; an unfinished swap (install.pending / import.part left over) never counts (P1-1, fourth audit A4-04).
// 3. 内部目录有主程序时只看内部；没有才看外部私有目录（与 startHost / jni_android.c 同一条规则）。
//    When internal has the main module only internal counts; otherwise the external private directory is looked at (the same rule as startHost /
//    jni_android.c).
final class StartupDecision {
    private StartupDecision() {}

    // 原生层启动前核的几件（jni_android.c）：六件程序、至少一个着色器包、触屏覆盖层 | what the native layer checks before startup (jni_android.c): the six program files, at least one shader bundle, the touch overlay
    static final String[] MODULES = {"main.patched.bin", "main.fixups.tsv", "main.gates.tsv", "sdk.patched.bin", "sdk.fixups.tsv", "sdk.gates.tsv"};

    enum Action { BOOT, IMPORT, GENERATE }

    // 生成熔断（P2-4 返工）：只数「连续失败」——一次生成结束而内容仍不齐就 +1；生成成功（内容齐全、可启动）或玩家点「再试一次」清零；
    // 升级到新版本（versionCode 变了）旧计数一律作废，从 0 起。上限仍是 2 次连续失败。
    // Generation circuit breaker (P2-4 rework): only *consecutive* failures count -- one build that finishes with the content still incomplete adds 1;
    // a successful build (content complete and bootable) or the player's "Try again" clears it; a new version code discards the old count and starts
    // from 0. The cap is still 2 consecutive failures.
    static final int GEN_TRIES_MAX = 2;

    enum GenOutcome { READY, FAILED, RETRY }

    // 一次生成结束后这一版的新计数 | the new count for this build after one generation round
    static int genTriesNext(GenOutcome outcome, long versionCode, long countedFor, int tries) {
        if (outcome != GenOutcome.FAILED) return 0;            // 成功 / 再试一次：清零 | success / try again: clear
        return (countedFor == versionCode ? tries : 0) + 1;    // 别的版本的旧计数不算 | counts from another version do not carry over
    }

    // 熔断只拦本版的连续失败链；升级后自然放行 | the breaker only blocks this build's consecutive-failure chain; a new version is free again
    static boolean generationLimitReached(long versionCode, long countedFor, int tries) {
        return countedFor == versionCode && tries >= GEN_TRIES_MAX;
    }

    static String readText(File f) {
        try { return new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8); } catch (IOException e) { return null; }
    }

    static String packProduct(String info) {
        if (info != null) for (String line : info.split("\n")) if (line.startsWith("PRODUCT=")) return line.substring(8).trim();
        return null;
    }

    // 这个运行目录能不能直接启动（与 jni_android.c 同一套内容检查，另加 derived.tsv 与版本记录，以及「换上没做完」标记）
    // Whether this runtime directory can boot directly (the same content checks as jni_android.c, plus derived.tsv, the version record and the "swap unfinished" marker)
    static boolean contentReady(File runtime, String product) {
        if (runtime == null || PackGenerator.installPending(runtime)) return false;
        File out = new File(runtime, "out");
        for (String m : MODULES) if (!new File(out, m).canRead()) return false;
        String[] names = new File(runtime, "shaders").list(); boolean bundle = false;
        if (names != null) for (String n : names) if (n.endsWith(".xgs")) { bundle = true; break; }
        if (!bundle || !new File(runtime, "overlay").isDirectory()) return false;
        // 原生库加载时就从这里读表（dm/host/dm_derived_rt.h）：缺了内容就是半份，不能直接启动 | the native library reads its tables from here at load time (dm/host/dm_derived_rt.h): without it the content is half a pack and must not boot
        if (!new File(runtime, "derived.tsv").canRead()) return false;
        // 没有版本记录（pack_info.txt）或被别的版本改过：不能当成齐全 | no version record (pack_info.txt), or one rewritten for another version: not complete
        String info = readText(new File(runtime, "pack_info.txt"));
        return info != null && product != null && product.equals(packProduct(info));
    }

    static Action decide(File source, File runtime, File extRuntime, String product, boolean generating) {
        if (generating) return Action.GENERATE;
        String want = source == null ? null : readText(new File(source, "pack_info.txt"));
        String have = runtime == null ? null : readText(new File(runtime, "pack_info.txt"));
        // 电脑做的导入包与这版 App 配套、且和已导入的不同：走导入流程 | a computer-made pack for this build that differs from the imported one: import it
        if (want != null && !want.equals(have) && product != null && product.equals(packProduct(want))) return Action.IMPORT;
        // 不配套的旧导入包到这里就当没有：不再因为它生成，只看运行目录（已有本版内容就启动，否则才生成）| an incompatible old import pack is treated as absent here:
        // it never triggers generation; only the runtime directory decides (boot when it already holds this build's content, otherwise generate)
        // 内部目录上次换上没做完：不能拿外部的旧内容顶上（P1-1，四审 A4-04）。只有 import.part 残留、没有 install.pending，说明是导入在删 part 之前
        // 被杀（P2-1）：下载目录里还有本版配套的导入包就重走导入（importPack 开头会清 part），没有才生成。
        // The last swap into internal did not finish: never fall back to old external content (P1-1, fourth audit A4-04). When only import.part is left with
        // no install.pending, the import was killed before deleting part (P2-1): redo the import when the download folder still holds this build's pack
        // (importPack clears part first), otherwise generate.
        if (runtime != null && PackGenerator.installPending(runtime)) {
            boolean importOnly = !new File(runtime, PackGenerator.PENDING).exists() && new File(runtime, "import.part").exists();
            if (importOnly && want != null && product != null && product.equals(packProduct(want))) return Action.IMPORT;
            return Action.GENERATE;
        }
        boolean internal = contentReady(runtime, product);
        boolean external = runtime != null && !new File(runtime, "out/main.patched.bin").canRead() && contentReady(extRuntime, product);
        return internal || external ? Action.BOOT : Action.GENERATE;
    }
}
