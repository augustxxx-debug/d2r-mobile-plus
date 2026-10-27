"""分享诊断日志的头部摘要要带上自定义设置读盘行（含显示帧数 fps_hud 等全部键），以及 120 帧档与光追阴影的生效行；iOS 在 perf_block，Mac 在 info.txt，两边都先脱敏再写出（本机规则「新功能上线前适配分享诊断日志」，2026-10-27）。
The diagnostic share header must carry the custom-settings load line (every key, including fps_hud) plus the 120 fps mode and ray-traced-shadow effective lines; iOS in perf_block, Mac in info.txt, both scrubbed before writing (local rule "adapt the diagnostic share before shipping a feature", 2026-10-27)."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SHARE = (ROOT / 'ios' / 'Sources' / 'dm_sharelog.m').read_text(encoding='utf-8')
MAC = (ROOT / 'host' / 'front_mac.m').read_text(encoding='utf-8')


def block(src, start):
    i = src.index(start); depth = 0; j = src.index('{', i)
    for k in range(j, len(src)):
        depth += src[k] == '{'; depth -= src[k] == '}'
        if depth == 0:
            return src[i:k + 1]
    raise AssertionError('unbalanced: ' + start)


class IosSummary(unittest.TestCase):
    def test_perf_block_has_settings_line(self):
        keys = re.search(r'static const char \*keys\[\] = \{([^}]*)\}', block(SHARE, 'static void perf_block(')).group(1)
        for k in ('"自定义设置：读盘"', '"图形设置："', '"帧率档：生效"', '"帧率档：出图"', '"光追阴影：机器判定"', '"光追阴影：生效"', '"光追阴影：回退"', '"光追阴影：第"'):
            self.assertIn(k, keys)


class MacSummary(unittest.TestCase):
    def test_info_has_settings_lines(self):
        share = block(MAC, 'static NSString *share_logs(NSString *to) {')
        for k in ('自定义设置：读盘', '图形设置：', '帧率档：生效', '帧率档：出图', '光追阴影：机器判定', '光追阴影：生效', '光追阴影：回退', '光追阴影：第'):
            self.assertIn(k, share)

    def test_summary_goes_through_scrub(self):
        share = block(MAC, 'static NSString *share_logs(NSString *to) {')
        i = share.index('自定义设置：读盘'); j = share.index('info.txt')
        self.assertLess(i, j, 'summary is built before info.txt is written')
        self.assertRegex(share, r'scrub\(\[info dataUsingEncoding')


if __name__ == '__main__':
    unittest.main(verbosity=1)
