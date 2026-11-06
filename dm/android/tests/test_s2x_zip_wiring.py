"""REQ28（二）：S2X.zip 首启自动解压接进 DmActivity（触发条件、后台线程、进度页、取消续传、六语文案）。

纯静态检查，不读游戏内容、不编译 Java、不跑 Gradle。
Static checks only: no game content is read, no Java is compiled, Gradle is not run.
"""
from pathlib import Path
import importlib.util
import re
import tempfile
import unittest
import xml.etree.ElementTree as ET

ANDROID = Path(__file__).resolve().parents[1]          # dm/android
REPO = ANDROID.parents[1]                              # 仓库根 | repository root
ACTIVITY = ANDROID / 'app/src/main/java/com/xiaoboluo/d2r/DmActivity.java'
LOADPAGE = ANDROID / 'app/src/main/java/com/xiaoboluo/d2r/LoadPageView.java'
S2XZIP = ANDROID / 'app/src/main/java/com/xiaoboluo/d2r/S2xZip.java'
TSV = REPO / 'dm/i18n/ui_text.tsv'
TOOLS = ANDROID / 'tools'

# 本卡新增的六语文案键（标题 / 正文格式 / 缺项 / 空间不足 / 暂停 / 暂停正文 / 继续按钮 / 拒绝原因 / 失败页标题 / 解压中断）
# i18n keys added by this card (title / body format / missing items / no space / paused / paused body / resume / reject / failure title / interrupted)
ZIP_KEYS = (
    'dmAndroidZipTitle', 'dmAndroidZipProgress', 'dmAndroidZipNeedFiles', 'dmAndroidZipNoSpace',
    'dmAndroidZipPaused', 'dmAndroidZipPausedBody', 'dmAndroidZipResume', 'dmAndroidZipReject',
    'dmAndroidZipFailTitle', 'dmAndroidZipFailed',
)
FOLDERS = {'values': 2, 'values-zh': 1, 'values-es': 3, 'values-es-rMX': 4, 'values-ru': 5, 'values-ko': 6}


def strip_comments(text):
    """去掉 // 与 /* */ 注释：注释里的说明不算接线。 | Drop comments; remarks in comments do not count as wiring."""
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


def read(path):
    return {e.get('name'): (e.text or '') for e in ET.parse(path).getroot()}


class S2xZipWiring(unittest.TestCase):
    def setUp(self):
        self.code = strip_comments(ACTIVITY.read_text(encoding='utf-8'))

    def zip_body(self):
        # 线程体在 startZip 里起、活在 runZip 里（Opus 审 P2-3 拆出来以便整段接 Throwable）| the thread starts in startZip and its work lives in runZip (split by Opus review P2-3 so the whole body catches Throwable)
        return method_body(self.code, 'private void startZip(File zip, File dest)') + method_body(self.code, 'private static void runZip(')

    def test_worker_catches_everything(self):
        start = re.sub(r'\s+', ' ', method_body(self.code, 'private void startZip(File zip, File dest)'))
        self.assertIn('try { runZip(app, zip, dest, progressFormat); } catch (Throwable t)', start)
        self.assertIn('finishZipFailure(', start[start.index('catch (Throwable t)'):])

    def test_trigger_is_the_pure_should_offer_and_runs_before_the_old_decision(self):
        # 有权限、准备判定之前：ensureZipUnpacked 用纯函数 shouldOffer(missingInputs, find(downloads))；文件齐了它放行
        # With permission and before the preparation decision: ensureZipUnpacked uses the pure shouldOffer(missingInputs, find(downloads));
        # complete files pass straight through
        body = method_body(self.code, 'private boolean ensureZipUnpacked(File downloads, File gameBase)')
        self.assertIn('PackGenerator.missingInputs(gameBase)', body)
        self.assertIn('S2xZip.find(downloads)', body)
        self.assertIn('S2xZip.shouldOffer(missing, zip)', body)
        self.assertIn('if (!S2xZip.shouldOffer(missing, zip)) return true;', body)
        start = method_body(self.code, 'private void startHost()')
        self.assertIn('ensureFileAccess()', start)
        self.assertIn('if (!libraryLoaded && !ensureZipUnpacked(downloads, gameBase)) return;', start)
        # 顺序：权限 → 解压 → 原有导入 / 生成判定 | order: permission -> unzip -> the existing import / generate decision
        self.assertLess(start.index('ensureFileAccess()'), start.index('ensureZipUnpacked(downloads, gameBase)'))
        self.assertLess(start.index('ensureZipUnpacked(downloads, gameBase)'), start.index('ensureImported('))

    def test_zip_work_runs_on_a_background_thread(self):
        # plan / spaceNeeded / extract 全在 "dm-s2x-zip" 线程里；主线程只投递界面
        # plan / spaceNeeded / extract all run on the "dm-s2x-zip" thread; the main thread only receives UI work
        body = self.zip_body()
        self.assertIn('new Thread(', body)
        self.assertIn('"dm-s2x-zip"', body)
        self.assertIn('S2xZip.plan(zip)', body)
        self.assertIn('S2xZip.spaceNeeded(plan, dest)', body)
        self.assertIn('S2xZip.extract(plan, dest,', body)
        thread = body.index('new Thread(')
        self.assertLess(thread, body.index('S2xZip.plan(zip)'))
        self.assertLess(thread, body.index('S2xZip.spaceNeeded(plan, dest)'))
        self.assertLess(thread, body.index('S2xZip.extract(plan, dest,'))
        self.assertIn('postUi(', body)   # 结果只经 postUi 投递主线程 | results reach the main thread only through postUi
        # 线程真的被 start()，不是在主线程上直接 run()（OMP 变异 M3）| the thread is really start()ed, not run() on the main thread (OMP mutation M3)
        self.assertRegex(re.sub(r'\s+', ' ', body), r'\}, "dm-s2x-zip"\)\.start\(\);')
        self.assertNotIn('.run();', body)

    def test_zip_done_guard_and_both_cancel_paths(self):
        # 本进程解过一次就不再触发（防解完仍缺文件时反复解压，OMP 变异 M6）| once per process (stops a loop when files are still missing after a full pass, OMP mutation M6)
        body = method_body(self.code, 'private boolean ensureZipUnpacked(File downloads, File gameBase)')
        self.assertIn('missing.isEmpty() || zipDone ? null : S2xZip.find(downloads)', re.sub(r'\s+', ' ', body))
        self.assertIn('zipDone = true', self.zip_body())
        # 加载页没挂上（或不认这个键）时，B / 返回也要能取消（OMP 变异 M1）| B / Back must cancel even when the loading page is not attached or ignores the key (OMP mutation M1)
        keys = re.sub(r'\s+', ' ', method_body(self.code, 'public boolean dispatchKeyEvent('))
        self.assertIn('if (!started && zipRunning && (e.getKeyCode() == android.view.KeyEvent.KEYCODE_BACK || e.getKeyCode() == android.view.KeyEvent.KEYCODE_BUTTON_B)) { zipCancel.set(true); return true; }', keys)

    def test_four_outcomes_have_their_screens(self):
        # 结构不对 / 空间不够 → 失败页（showGenScreen）；否则加载页；取消 → 暂停页；成功 → 照常进现有流程
        # structurally wrong / not enough space -> failure page (showGenScreen); otherwise the loading page; cancel -> paused page;
        # success -> the existing flow as usual
        body = self.zip_body()
        self.assertIn('!plan.ok()', body)
        self.assertIn('R.string.dmAndroidZipNeedFiles', body)
        self.assertIn('R.string.dmAndroidZipReject', body)
        self.assertIn('usableSpace(dest)', body)
        self.assertIn('R.string.dmAndroidZipNoSpace', body)
        self.assertIn('R.string.dmAndroidZipTitle', body)
        self.assertIn('LoadPageView.begin(', body)
        self.assertIn('LoadPageView.set(', body)
        self.assertIn('LoadPageView.note(', body)
        self.assertIn('LoadPageView.finish()', body)
        self.assertIn('R.string.dmAndroidZipPaused', self.code)
        self.assertIn('R.string.dmAndroidZipResume', self.code)
        paused = method_body(self.code, 'private void showZipPaused()')
        self.assertIn('resumeZip()', paused)
        failure = method_body(self.code, 'private void showZipFailure()')
        self.assertIn('showGenScreen(', failure)
        # 加载页标题覆盖只在画字层（公共核心的阶段标题是固定键）| the loading-page title override lives in the drawing layer only
        self.assertIn('titleOverride', LOADPAGE.read_text(encoding='utf-8'))

    def test_back_and_b_cancel_during_extraction(self):
        # 解压中 B / 返回键置 AtomicBoolean；去掉这两行本测试必须失败（变异自证）
        # During extraction B / Back set the AtomicBoolean; removing those two lines must fail this test (mutation check)
        body = method_body(self.code, 'public boolean dispatchKeyEvent(android.view.KeyEvent e)')
        self.assertIn('zipRunning', body)
        self.assertIn('zipCancel.set(true)', body)
        self.assertIn('KEYCODE_BACK', body)
        self.assertIn('KEYCODE_BUTTON_B', body)
        # 暂停页手柄 A 松开 = 继续解压 | paused page: controller A on release = resume
        self.assertIn('zipPaused', body)
        self.assertIn('KEYCODE_BUTTON_A', body)
        self.assertIn('resumeZip()', body)

    def test_six_language_keys_are_complete(self):
        spec = importlib.util.spec_from_file_location('dm_i18n', REPO / 'dm/ios/tools/dm_i18n.py')
        table = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(table)
        rows = {r[0]: r for r in table.rows()}
        for key in ZIP_KEYS:
            self.assertIn(key, rows, key)
            self.assertTrue(all(rows[key][i] for i in range(1, 7)), key)   # 六套都非空 | all six sets present

        spec = importlib.util.spec_from_file_location('gen_strings', TOOLS / 'gen_strings.py')
        strings = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(strings)
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            strings.generate(root)
            for folder in FOLDERS:
                got = read(root / folder / 'strings.xml')
                for key in ZIP_KEYS:
                    self.assertIn(key, got, (folder, key))


if __name__ == '__main__':
    unittest.main()
