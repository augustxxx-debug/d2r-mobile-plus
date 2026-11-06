"""生成失败页 / 熔断页：手柄 A 按当前焦点分发（terra B134 增量终审 P2）。静态检查 dispatchKeyEvent 的接线，判定本身由 GenScreenKeysTest 测。
Failure / breaker page: controller A follows the current focus (terra B134 incremental review P2). Static check of the dispatchKeyEvent wiring;
the decision itself is tested by GenScreenKeysTest."""
from pathlib import Path
import re
import unittest

ACTIVITY = Path(__file__).resolve().parents[1] / 'app/src/main/java/com/xiaoboluo/d2r/DmActivity.java'


def code():
    t = ACTIVITY.read_text(encoding='utf-8')
    t = re.sub(r'/\*.*?\*/', '', t, flags=re.S)
    return re.sub(r'//[^\n]*', '', t)


def body(src, sig):
    i = src.index('{', src.index(sig)); d = 0
    for j in range(i, len(src)):
        d += {'{': 1, '}': -1}.get(src[j], 0)
        if d == 0:
            return src[i:j + 1]
    raise AssertionError(sig)


class GenScreenFocus(unittest.TestCase):
    def test_a_key_follows_focus(self):
        b = body(code(), 'public boolean dispatchKeyEvent(')
        seg = b[b.index('genFailure != null && genLogText != null'):]
        seg = seg[:seg.index('return true;')]
        self.assertIn('getCurrentFocus()', seg)
        self.assertIn('GenScreenKeys.aTarget(', seg)
        # 焦点必须真按按钮对象认，不许被写死成 NONE | the focus must really be matched against the button objects, never hard-wired to NONE
        flat = re.sub(r'\s+', ' ', seg)
        self.assertRegex(flat, r'int focused = f != null && f == genShareBtn \? GenScreenKeys\.SHARE : f != null && f == genRetryBtn \? GenScreenKeys\.RETRY : GenScreenKeys\.NONE;')
        self.assertRegex(flat, r'GenScreenKeys\.aTarget\(focused,')
        # 动作接线：只经 GenScreenKeys.dispatch，参数顺序 (分享, 重试) 锁死；不许再直接调两种动作（terra T4 P2 交换变异）
        # action wiring: only through GenScreenKeys.dispatch with the (share, retry) argument order pinned; no direct calls to either action (terra T4 P2 swap mutation)
        self.assertRegex(flat, r'GenScreenKeys\.dispatch\(t, this::shareGenLog, this::retryGeneration\);')
        self.assertNotIn('retryGeneration()', seg)
        self.assertNotIn('shareGenLog()', seg)
        # 不许再按 genRetryOffered 一刀切 | no more blanket decision on genRetryOffered
        self.assertNotRegex(seg, r'if\s*\(\s*genRetryOffered\s*\)\s*retryGeneration\(\)')

    def test_buttons_recorded(self):
        b = body(code(), 'private void showGenScreen(')
        self.assertIn('genShareBtn = null', b)
        self.assertIn('genShareBtn = btn', b)
        self.assertIn('genRetryBtn = btn', b)


    def test_pad_hint_follows_focus(self):
        # 真机奥丁 60aac2e3：提示写死「按 A 分享」而熔断页 A 实为重试 → 提示按焦点走同一判定 | the hint follows the focus through the same decision
        b = re.sub(r'\s+', ' ', body(code(), 'private void updateGenPadHint()'))
        self.assertIn('GenScreenKeys.aTarget(focused,', b)
        self.assertIn('GenScreenKeys.aGlyph(genPadVendor)', b)
        self.assertIn('t == GenScreenKeys.RETRY ? R.string.dmAndroidGenRetry : R.string.dmAndroidGenShare', b)
        g = re.sub(r'\s+', ' ', body(code(), 'private void showGenScreen('))
        self.assertIn('if (genShareBtn != null) genShareBtn.setOnFocusChangeListener(fl);', g)
        self.assertIn('if (genRetryBtn != null) genRetryBtn.setOnFocusChangeListener(fl);', g)
        self.assertIn('updateGenPadHint();', g)

    def test_gen_log_always_in_new_folder(self):
        # 熔断页曾写 new File(base, "gen_log.txt")，旧目录有文件时落进 D2R Mobile | the breaker page once wrote into the old D2R Mobile folder
        c = code()
        self.assertNotIn('new File(base, "gen_log.txt")', c)
        self.assertGreaterEqual(c.count('PackGenerator.genLogFile(base)'), 3)


if __name__ == '__main__':
    unittest.main()
