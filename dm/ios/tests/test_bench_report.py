"""功耗报告的电量量化（审计 P2，2026-10-27）：步长显式或从数据推断、误差按步长、不可比不给速率、区间重叠不排名。
Battery quantization in the power report (audit P2, 2026-10-27): explicit or inferred step, error from the step, no rate when not comparable, no ranking when intervals overlap."""
import io
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import bench_report as br  # noqa: E402


def rows(levels, charging='未充电', every=5):
    return [{'sec': i * every, 'battery': float(b), 'charging': charging} for i, b in enumerate(levels)]


class Step(unittest.TestCase):
    def test_multiples_of_five_infer_five(self):
        self.assertEqual(br.infer_step(rows([60] * 50 + [55] * 50))[0], 5.0)

    def test_one_percent_change_infers_one(self):
        self.assertEqual(br.infer_step(rows([60, 60, 59, 59, 58]))[0], 1.0)

    def test_no_unplugged_data_is_conservative(self):
        step, src = br.infer_step(rows([60, 59], charging='充电中'))
        self.assertEqual(step, 5.0); self.assertIn('保守', src)


class Rate(unittest.TestCase):
    def test_ten_minutes_at_five_percent_is_thirty_per_hour(self):
        r = br.battery_rate(rows([60] * 60 + [55] * 61, every=5), 5.0)   # 600 s
        self.assertAlmostEqual(r['err'], 30.0, places=6); self.assertAlmostEqual(r['rate'], 30.0, places=6)

    def test_same_segment_at_one_percent_is_six(self):
        r = br.battery_rate(rows([60] * 60 + [59] * 61, every=5), 1.0)
        self.assertAlmostEqual(r['err'], 6.0, places=6)

    def test_charging_gives_no_rate(self):
        r = br.battery_rate(rows([60] * 121, charging='充电中'), 5.0)
        self.assertIsNone(r['rate']); self.assertIn('充电', r['reason'])

    def test_mixed_states_give_no_rate(self):
        part = rows([60] * 100) + [{'sec': 600, 'battery': 60.0, 'charging': '充电中'}]
        self.assertIsNone(br.battery_rate(part, 5.0)['rate'])

    def test_short_segment_gives_no_rate(self):
        r = br.battery_rate(rows([60] * 60, every=5), 5.0)   # 295 s
        self.assertIsNone(r['rate']); self.assertIn('太短', r['reason'])

    def test_too_few_samples(self):
        self.assertIsNone(br.battery_rate(rows([60]), 5.0)['rate'])


class Rank(unittest.TestCase):
    def test_overlap_refuses(self):
        self.assertIsNone(br.ranking([('A', 30.0, 30.0), ('B', 0.0, 30.0)]))

    def test_missing_rate_refuses(self):
        self.assertIsNone(br.ranking([('A', 30.0, 5.0), ('B', None, None)]))

    def test_separated_ranks_low_to_high(self):
        self.assertEqual(br.ranking([('A', 40.0, 5.0), ('B', 10.0, 5.0)]), ['B', 'A'])


class Report(unittest.TestCase):
    def run_report(self, heat, *extra):
        with tempfile.TemporaryDirectory() as d:
            Path(d, 'params.txt').write_text('run=t\nseg_s=600\nlevels=1,3\n', encoding='utf-8')
            Path(d, 'heat.tsv').write_text(heat, encoding='utf-8')
            out = io.StringIO(); argv = sys.argv
            sys.argv = ['bench_report.py', d, *extra]
            try:
                with redirect_stdout(out): br.main()
            finally:
                sys.argv = argv
            return out.getvalue()

    def heat(self):
        lines = ['h\t' * 13 + 'h']
        for i in range(250):
            lv = 1 if i < 125 else 3; b = 60 - 5 * (i // 60)
            lines.append('\t'.join(map(str, [i * 5, 1, 100, 60, 10, 11, 300, 0.5, 120, b, '未充电', 0, lv, 2])))
        return '\n'.join(lines) + '\n'

    def test_explicit_step_is_reported(self):
        out = self.run_report(self.heat(), '--battery-step', '1')
        self.assertIn('**1%**', out); self.assertIn('显式参数', out)

    def test_inferred_step_and_no_overlapping_ranking(self):
        out = self.run_report(self.heat())
        self.assertIn('**5%**', out); self.assertIn('功耗排名**：不给', out)
        self.assertNotIn('电量按 1% 量化', out)


class Segments(unittest.TestCase):
    """复审 F04：掉帧等按档位序列对齐，不按下标；对不上标“无法归属” | re-audit F04: align by level sequence, never by index; unmatched → unassignable"""
    def build(self, d, heat_levels, boot_levels):
        Path(d, 'params.txt').write_text('run=t\nseg_s=610\nlevels=0,1,3\n', encoding='utf-8')
        lines = ['h\t' * 13 + 'h']; t = 0
        for lv, secs in heat_levels:
            for _ in range(secs // 5):
                lines.append('\t'.join(map(str, [t, 1, 100, 60, 10, 11, 300, 0.5, 120, 60, '未充电', 0, lv, 2]))); t += 5
        Path(d, 'heat.tsv').write_text('\n'.join(lines) + '\n', encoding='utf-8')
        boot = []
        for k, lv in enumerate(boot_levels):
            boot.append(f'[dm] 测试 DM_TEST_RENDER_RES：第 {k * 600} 秒 → 渲染分辨率档位 {lv}')
            boot.append(f'[dm] [nvn] 上屏 至帧 1：1000 帧，掉帧 {10 * (k + 1)}（丢弃 0），间隔')
        Path(d, 'boot.txt').write_text('\n'.join(boot) + '\n', encoding='utf-8')

    def report(self, heat_levels, boot_levels):
        with tempfile.TemporaryDirectory() as d:
            self.build(d, heat_levels, boot_levels)
            out = io.StringIO(); argv = sys.argv; sys.argv = ['bench_report.py', d]
            try:
                with redirect_stdout(out): br.main()
            finally:
                sys.argv = argv
            return out.getvalue()

    def row(self, out, name):
        return next(l for l in out.splitlines() if l.startswith('| ' + name))

    def test_short_leading_segment_does_not_shift(self):
        out = self.report([(0, 20), (1, 610), (3, 610)], [0, 1, 3])
        self.assertIn('20/1000', self.row(out, '省电')); self.assertIn('30/1000', self.row(out, '原生'))
        self.assertNotIn('自动', '\n'.join(l for l in out.splitlines() if l.startswith('| ')))   # 20 秒的段不显示 | the 20 s run is hidden

    def test_mismatched_log_is_unassignable(self):
        out = self.report([(0, 20), (1, 610), (3, 610)], [1, 3])
        self.assertIn('无法归属', self.row(out, '省电')); self.assertIn('对不上', out)

    def test_unknown_battery_sample_blocks_rate(self):
        part = rows([60] * 60 + [55] * 61, every=5); part[60]['battery'] = -1.0
        r = br.battery_rate(part, 5.0)
        self.assertIsNone(r['rate']); self.assertIn('读不到电量', r['reason'])


class MissingGpu(unittest.TestCase):
    """二审 R01：GPU 样本数为 0 的行不能把同一行的电量 / 充电 / 帧率缺口一起删掉；每种缺失组合独立检验
    Second review R01: a row with zero GPU samples must not delete the same row's battery / charging / fps gaps; each missing combination is tested on its own"""
    def heat(self, patch):
        lines = ['h\t' * 13 + 'h']
        for i in range(133):   # 660 秒，单段 | 660 s, one segment
            r = {'sec': i * 5, 'fps': 60, 'gpu': 10, 'gpu95': 11, 'n': 300, 'battery': 60 - 5 * (i // 66), 'charging': '未充电'}
            if i * 5 == 300: r.update(patch)
            lines.append('\t'.join(map(str, [r['sec'], 1, 100, r['fps'], r['gpu'], r['gpu95'], r['n'], 0.5, 120, r['battery'], r['charging'], 0, 1, 2])))
        return '\n'.join(lines) + '\n'

    def report(self, patch):
        with tempfile.TemporaryDirectory() as d:
            Path(d, 'params.txt').write_text('run=t\nseg_s=660\nlevels=1\n', encoding='utf-8')
            Path(d, 'heat.tsv').write_text(self.heat(patch), encoding='utf-8')
            out = io.StringIO(); argv = sys.argv; sys.argv = ['bench_report.py', d]
            try:
                with redirect_stdout(out): br.main()
            finally:
                sys.argv = argv
            return next(l for l in out.getvalue().splitlines() if l.startswith('| 省电'))

    def cells(self, row):
        return [c.strip() for c in row.strip('|').split('|')]

    def test_reviewer_scenario_gpu0_fps0_battery_unknown_charging_unknown(self):
        row = self.cells(self.report({'n': 0, 'gpu': 0, 'gpu95': 0, 'fps': 0, 'battery': -1, 'charging': '未知'}))
        self.assertIn('不给', row[3]); self.assertNotEqual(row[5], '60.0')

    def test_gpu0_only_keeps_battery_and_marks_gpu(self):
        row = self.cells(self.report({'n': 0, 'gpu': 0, 'gpu95': 0}))
        self.assertNotIn('不给', row[3]); self.assertEqual(row[5], '60.0')
        self.assertTrue(row[6].startswith('10.00 / 11.00'), row[6]); self.assertIn('1/', row[6])

    def test_unknown_battery_alone_blocks_rate(self):
        self.assertIn('读不到电量', self.cells(self.report({'battery': -1}))[3])

    def test_unknown_charging_alone_blocks_rate(self):
        self.assertIn('未知', self.cells(self.report({'charging': '未知'}))[3])

    def test_low_fps_window_lowers_mean_even_without_gpu(self):
        self.assertNotEqual(self.cells(self.report({'n': 0, 'fps': 0}))[5], '60.0')

    def test_all_windows_without_gpu_still_reports_other_metrics(self):
        with tempfile.TemporaryDirectory() as d:
            Path(d, 'params.txt').write_text('run=t\nseg_s=660\nlevels=1\n', encoding='utf-8')
            Path(d, 'heat.tsv').write_text(self.heat({}).replace('\t300\t', '\t0\t'), encoding='utf-8')
            out = io.StringIO(); argv = sys.argv; sys.argv = ['bench_report.py', d]
            try:
                with redirect_stdout(out): br.main()
            finally:
                sys.argv = argv
            row = self.cells(next(l for l in out.getvalue().splitlines() if l.startswith('| 省电')))
        self.assertEqual(row[6], '无 GPU 样本'); self.assertEqual(row[5], '60.0'); self.assertNotIn('不给', row[3])


if __name__ == '__main__':
    unittest.main(verbosity=1)
