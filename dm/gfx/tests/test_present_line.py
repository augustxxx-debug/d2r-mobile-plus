"""「上屏 至帧」行全程带 footprint（真机长测 6a777d0：120 帧走显示节拍时 7500 帧后呈现回调不再给时间，整行不打，之后 1300 秒没有 footprint 采样）。
hle_nvn.c tsummary：有呈现读数打原来那一行；没有读数的窗口也打一行（帧数 = 丢弃数，带「呈现时间无读数」和 footprint），每 300 帧都有 footprint。
bench_report.py 的掉帧解析照样认这一行（没有读数的窗口不算帧、不算掉帧）。
The "上屏 至帧" line carries footprint throughout (device long test 6a777d0: at 120 fps through the display link the presented callback stopped giving times after frame 7500, the whole line stopped, and no footprint was sampled for 1300 s).
hle_nvn.c tsummary: with readings the original line; a window without readings also logs a line (frames = discards, with "呈现时间无读数" and footprint), so every 300 frames carries footprint.
bench_report.py's drop parsing still accepts the line (a window without readings counts no frames and no drops)."""
import re
import sys
import unittest
from pathlib import Path

DM = Path(__file__).resolve().parents[2]
SRC = (DM / 'gfx' / 'nvn' / 'hle_nvn.c').read_text(encoding='utf-8')
sys.path.insert(0, str(DM / 'ios' / 'tools'))


def tsummary():
    i = SRC.index('static void tsummary(uint64_t frame) {'); depth = 0; j = SRC.index('{', i)
    for k in range(j, len(SRC)):
        depth += SRC[k] == '{'; depth -= SRC[k] == '}'
        if depth == 0:
            return SRC[i:k + 1]


class PresentLine(unittest.TestCase):
    def test_both_branches_log_footprint(self):
        body = tsummary()
        lines = re.findall(r'LOG\("\[nvn\] 上屏 至帧[^;]*', body)
        self.assertEqual(len(lines), 2, lines)
        for l in lines:
            self.assertIn('footprint %.0f MiB', l)
        self.assertRegex(body, r'if \(k\) \{[^\n]*LOG\("\[nvn\] 上屏 至帧')
        self.assertRegex(body, r'\n\s*else LOG\("\[nvn\] 上屏 至帧 %llu：%u 帧，掉帧 %u（丢弃 %u），呈现时间无读数；footprint')

    def test_bench_report_parses_no_reading_window(self):
        import bench_report
        boot = ['[dm] 测试 DM_TEST_RENDER_RES：第 0 秒 → 渲染分辨率档位 3',
                '[dm] [nvn] 上屏 至帧 300：300 帧，掉帧 2（丢弃 1），间隔 p5 8.33 / p50 8.33 / p95 8.33，p99 12.50 ms；footprint 2400 MiB；亮度 0.90；温控 1',
                '[dm] [nvn] 上屏 至帧 600：0 帧，掉帧 0（丢弃 0），呈现时间无读数；footprint 2410 MiB；亮度 0.90；温控 1']
        import tempfile, os
        with tempfile.TemporaryDirectory() as d:
            open(os.path.join(d, 'boot.txt'), 'w', encoding='utf-8').write('\n'.join(boot) + '\n')
            out = bench_report.dropped_by_segment(d)
        self.assertEqual((out[0]['frames'], out[0]['dropped'], out[0]['discarded']), (300, 2, 1))


if __name__ == '__main__':
    r = unittest.main(exit=False, verbosity=1).result
    sys.exit(1 if (not r.testsRun or r.failures or r.errors) else 0)
