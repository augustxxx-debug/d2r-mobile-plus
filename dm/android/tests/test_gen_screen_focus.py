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
        # 不许再按 genRetryOffered 一刀切 | no more blanket decision on genRetryOffered
        self.assertNotRegex(seg, r'if\s*\(\s*genRetryOffered\s*\)\s*retryGeneration\(\)')

    def test_buttons_recorded(self):
        b = body(code(), 'private void showGenScreen(')
        self.assertIn('genShareBtn = null', b)
        self.assertIn('genShareBtn = btn', b)
        self.assertIn('genRetryBtn = btn', b)


if __name__ == '__main__':
    unittest.main()
