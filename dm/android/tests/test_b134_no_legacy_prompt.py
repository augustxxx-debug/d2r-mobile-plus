"""B134：没有可用游戏内容时一律进加载页（手机端生成流程），玩家路径不再出现旧版「电脑端导入」纯文字提示。
B134: with no usable game content the loading page (on-phone generation) always runs; the player-visible path no longer
shows the legacy computer-import plain-text prompt.

纯静态检查，不读游戏内容、不编译 Java、不跑 Gradle。
Pure static checks; no game content is read, no Java is compiled, Gradle is not run.
"""
from pathlib import Path
import re
import unittest

ANDROID = Path(__file__).resolve().parents[1]          # dm/android
REPO = ANDROID.parents[1]                              # 仓库根 | repository root
ACTIVITY = ANDROID / 'app/src/main/java/com/xiaoboluo/d2r/DmActivity.java'
DECISION = ANDROID / 'app/src/main/java/com/xiaoboluo/d2r/StartupDecision.java'
TSV = REPO / 'dm/i18n/ui_text.tsv'
HEADER = REPO / 'dm/host/dm_i18n_tab.h'

# 旧提示用到的多语键（都提到电脑端做导入包 / d2rm_android.py） | i18n keys of the legacy prompt (all mention the computer-made pack / d2rm_android.py)
LEGACY_KEYS = {
    'dmAndroidMissingModules', 'dmAndroidMissingShaders', 'dmAndroidMissingOverlay',
    'dmAndroidInvalidOverlay', 'dmAndroidImportVersion',
}
# 玩家能看到的旧文案（中英各一句就够定位） | the legacy wording a player could see (one Chinese, one English sentence is enough to pin it down)
OLD_PHRASES = (
    '还没有导入游戏内容',
    '请在电脑上用 d2rm_android.py',
    '做出导入包（android 文件夹）',
    'Game content has not been imported yet',
)


def strip_comments(text):
    """去掉 // 与 /* */ 注释：注释里的旧说法属于开发者文档，不算玩家可见路径。
    Drop // and /* */ comments: legacy wording in comments is developer documentation, not a player-visible path."""
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def method_body(code, signature):
    """取签名起、配平大括号为止的方法体（签名只在代码里出现一次）。 | Body from the signature to the matching closing brace (the signature appears once in code)."""
    start = code.index(signature)
    open_brace = code.index('{', start)
    depth = 0
    for i in range(open_brace, len(code)):
        if code[i] == '{':
            depth += 1
        elif code[i] == '}':
            depth -= 1
            if depth == 0:
                return code[open_brace:i + 1]
    raise AssertionError('unbalanced braces after ' + signature)


class NoLegacyPrompt(unittest.TestCase):
    def setUp(self):
        self.code = strip_comments(ACTIVITY.read_text(encoding='utf-8'))
        self.decision = strip_comments(DECISION.read_text(encoding='utf-8'))

    def test_player_strings_have_no_legacy_prompt(self):
        # 玩家看的文字全部来自统一表与它生成的头文件：旧提示与脚本名都不许留 | every player-visible string comes from the shared table and its header: no legacy prompt, no script name
        for name, path in (('ui_text.tsv', TSV), ('dm_i18n_tab.h', HEADER)):
            text = path.read_text(encoding='utf-8')
            for phrase in OLD_PHRASES:
                self.assertTrue(phrase not in text, '%s: %s' % (name, phrase))
            for key in LEGACY_KEYS:
                self.assertTrue(key not in text, '%s: %s' % (name, key))
            self.assertTrue('d2rm_android.py' not in text, name)

    def test_activity_code_never_shows_legacy_prompt(self):
        for key in LEGACY_KEYS:
            self.assertTrue('R.string.' + key not in self.code, key)
        for phrase in OLD_PHRASES:
            self.assertTrue(phrase not in self.code, phrase)
        self.assertTrue('d2rm_android.py' not in self.code)

    def test_no_content_always_enters_loading_page(self):
        # 没有可用内容时不能再看生成器在不在（generatorAvailable）或开发推送（pushedByDeveloper）决定是否进加载页
        # With no usable content, whether the generator is bundled (generatorAvailable) or content was pushed by a developer (pushedByDeveloper)
        # must not decide whether the loading page runs.
        body = method_body(self.code, 'private boolean ensureImported(File source)')
        self.assertIn('StartupDecision.Action action = StartupDecision.decide(source, runtime', body)
        self.assertIn('if (action == StartupDecision.Action.GENERATE)', body)
        self.assertIn('generate(source.getParentFile(), runtime)', body)
        self.assertNotIn('generatorAvailable()', body)
        self.assertNotIn('pushedByDeveloper', body)
        # B134 返工 P0-1：不配套的旧导入包不再有单独的 generate 分支；生成只由运行目录的实际内容决定。
        # B134 rework P0-1: an incompatible old import pack no longer has its own generate branch; only the runtime directory decides generation.
        self.assertNotIn('!BuildConfig.DM_PRODUCT.equals(product)', body)
        self.assertNotIn('packProduct(want)', body)
        # 与本版配套的电脑导入包仍走导入流程 | a computer pack for this build still goes through the import flow
        self.assertIn('StartupDecision.Action.IMPORT', body)
        # 开发时直推到外部私有目录的完整内容也算「有可用内容」：按 startHost / jni_android.c 的运行目录规则，内部没有主程序时看外部。
        # Complete content pushed directly to the external private directory also counts as usable: follow startHost / jni_android.c's runtime-directory
        # rule and look outside when internal has no main module.
        self.assertIn('getExternalFilesDir(null)', body)
        self.assertIn('new File(ext, "dm")', body)

    def test_game_content_check_matches_native(self):
        # contentReady 与 jni_android.c 核同一套：六件程序、至少一个 .xgs、overlay 目录（-3 / -4 / -9）；
        # 另加 derived.tsv 与 pack_info.txt 版本记录、换上一半的标记（B134 返工 P1-1）。
        # contentReady checks the same set as jni_android.c: the six program files, at least one .xgs, the overlay directory (-3 / -4 / -9);
        # it also requires derived.tsv, the pack_info.txt version record and no unfinished-swap marker (B134 rework P1-1).
        body = method_body(self.decision, 'static boolean contentReady(File runtime, String product)')
        self.assertIn('MODULES', body)
        self.assertIn(".endsWith(\".xgs\")", body)
        self.assertIn('new File(runtime, "overlay").isDirectory()', body)
        self.assertIn('new File(runtime, "derived.tsv").canRead()', body)
        self.assertIn('new File(runtime, "pack_info.txt")', body)
        self.assertIn('PackGenerator.installPending(runtime)', body)
        for module in ('main.patched.bin', 'main.fixups.tsv', 'main.gates.tsv',
                       'sdk.patched.bin', 'sdk.fixups.tsv', 'sdk.gates.tsv'):
            self.assertIn('"%s"' % module, self.decision)

    def test_native_missing_content_codes_enter_loading_page(self):
        # 生成能修好的只有缺程序 / 着色器 / 触屏资源（-3、-4、-9、-10）；缺数据（-2）与缺 build 配置（-5）是玩家资源目录的问题，
        # 不重开进程，直接显示指路文案（B134 返工 P2-1）。
        # Generation can only fix missing program / shaders / touch resources (-3, -4, -9, -10); missing data (-2) and build configuration (-5)
        # are the player's resource directory, so no relaunch and the guidance text shows instead (B134 rework P2-1).
        missing = method_body(self.code, 'static boolean contentMissing(int code)')
        for code in ('-3', '-4', '-9', '-10'):
            self.assertIn('code == ' + code, missing)
        self.assertNotIn('code == -2', missing)
        self.assertNotIn('code == -5', missing)
        start = method_body(self.code, 'private void startHost()')
        self.assertIn('if (result == -2 || result == -5)', start)
        self.assertIn('result == -2 ? R.string.dmAndroidMissingData : R.string.dmAndroidMissingConfig', start)
        self.assertIn('contentMissing(result)', start)
        # 本进程库已加载，不能就地生成：换干净进程，下次启动前由 ensureImported 进加载页 | the library is already loaded here, so do not generate in place:
        # relaunch a clean process; before the next launch ensureImported enters the loading page
        self.assertIn('relaunchForMissingContent()', start)
        relaunch = method_body(self.code, 'private boolean relaunchForMissingContent()')
        self.assertIn('content_regen_for', relaunch)
        self.assertIn('relaunchClean()', relaunch)
        self.assertIn('VERSION_CODE', relaunch)
        # 每个版本只重开一次：先比标记、命中就 return false 再写入；第二次落到通用启动失败页，不会来回重开。
        # At most one relaunch per version: compare the marker and return false on a hit before writing; the second time falls through to the generic
        # start-failure page, so there is no ping-pong relaunch.
        get_at = relaunch.index('getLong("content_regen_for", 0)')
        put_at = relaunch.index('putLong("content_regen_for"')
        self.assertLess(get_at, put_at, relaunch)
        self.assertIn('== BuildConfig.VERSION_CODE) return false;', relaunch)
        self.assertNotIn('postDelayed', relaunch)
        self.assertNotIn('generate(', relaunch)
        # 重开一次后（或标记已用过）落到通用启动失败页 | after one relaunch (or when the marker is used up) the generic start-failure page shows
        self.assertIn('showStartError(getString(R.string.dmAndroidStartFailed, result)); return;', start)

    def test_without_generator_generation_has_a_failure_exit(self):
        # 不带生成器的包（generatorAvailable() 为假）：generate() 开头就分流到专门的失败文案，不再进加载页空等，也不说「重新打开会接着生成」（P2-2）；
        # 缺游戏文件仍先 showGenFailure，生成线程里的 IOException / ErrnoException / RuntimeException 仍被捕获成 genFailure。
        # A package without the generator (generatorAvailable() is false): generate() branches at the top to a dedicated failure text, no longer waits
        # on the loading page and never says "reopen to continue" (P2-2); missing game files still go to showGenFailure, and IOException / ErrnoException /
        # RuntimeException in the generation thread are still caught into genFailure.
        gen = method_body(self.code, 'private void generate(File base, File runtime)')
        self.assertIn('failEarly(', gen)
        self.assertIn('catch (IOException | ErrnoException | InterruptedException | RuntimeException e)', gen)
        self.assertIn('genFailure = failure', gen)
        self.assertIn('generating = false', gen)
        at = gen.index('if (!generatorAvailable())')
        self.assertLess(at, gen.index('showLoadPage()'))
        branch = gen[at:gen.index('showLoadPage()', at)]
        self.assertIn('R.string.dmAndroidGenNoGenerator', branch)
        self.assertIn('showGenFailure()', branch)
        self.assertNotIn('dmAndroidGenInterrupted', branch)
        early = method_body(self.code, 'private void failEarly(File base, File data, String title, String reason, String why)')
        self.assertIn('showGenFailure()', early)

    def test_generation_failure_does_not_loop(self):
        # 生成失败只停在文字失败页：轮询看到 genFailure 就 showGenFailure 后 return，不再 postDelayed；失败路径不重开进程、不再次生成。
        # A failed generation stops on the text failure page: the poll sees genFailure, calls showGenFailure and returns without postDelayed; the failure
        # path neither relaunches the process nor generates again.
        poll = method_body(self.code, 'private final Runnable genPoll = new Runnable()')
        self.assertIn('showGenFailure(); return;', poll)
        self.assertLess(poll.index('showGenFailure()'), poll.index('genHandler.postDelayed'), poll)
        self.assertNotIn('relaunchClean', poll)
        gen = method_body(self.code, 'private void generate(File base, File runtime)')
        self.assertNotIn('relaunchClean', gen)
        self.assertNotIn('relaunchForMissingContent', gen)


if __name__ == '__main__':
    unittest.main()
