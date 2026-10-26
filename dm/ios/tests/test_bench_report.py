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


if __name__ == '__main__':
    unittest.main(verbosity=1)
