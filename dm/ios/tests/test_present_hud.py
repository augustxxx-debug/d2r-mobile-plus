"""复审 F03 / F08（2026-10-27）的源码级回归：
F03 / 二审 R03：DM_RATE_HINT=0 只去掉刷新率表态，触控 / 布局工作由主队列定时器按目标帧率（30 / 60 / 120）驱动；
F08 / 二审 R04：显示帧数标签读系统确认上屏的计数（presentedTime > 0 才计），计数器跨线程读写（含截图请求）走原子操作。
Source-level regression for re-audit F03 / F08 (2026-10-27):
F03: DM_RATE_HINT=0 drops only the refresh-rate declaration; touch / layout work keeps running from a main-thread timer;
F08: the frame-rate label reads the system-confirmed presented count (counted only when presentedTime > 0); the counters use atomic access across threads."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FRONT = (ROOT / 'ios' / 'Sources' / 'front_ios.m').read_text(encoding='utf-8')
MTL = (ROOT / 'gfx' / 'nvn' / 'nvn_mtl.m').read_text(encoding='utf-8')


def block(src, start):
    i = src.index(start); depth = 0; j = src.index('{', i)
    for k in range(j, len(src)):
        depth += src[k] == '{'; depth -= src[k] == '}'
        if depth == 0:
            return src[i:k + 1]
    raise AssertionError('unbalanced: ' + start)


class RateHint(unittest.TestCase):
    def test_tick_declares_then_delegates_to_work(self):
        tick = block(FRONT, '- (void)tick:(CADisplayLink *)l {')
        self.assertIn('preferredFrameRateRange', tick); self.assertIn('[self work]', tick)
        self.assertNotIn('tickTouchControls', tick)

    def test_work_does_touch_and_layout(self):
        work = block(FRONT, '- (void)work {')
        for needed in ('tickTouchControls', 'refreshPortalBinding', 'setNeedsLayout'):
            self.assertIn(needed, work)
        self.assertNotIn('preferredFrameRateRange', work)

    def test_rate_hint_off_still_drives_work(self):
        i = FRONT.index('if (!getenv("DM_RATE_HINT") || atoi(getenv("DM_RATE_HINT")))')
        j = FRONT.index('} else {', i) + 2
        off = block(FRONT[j:], 'else {')
        self.assertIn('[worker work]', off)
        # 二审 R03：不得固定 60 Hz；定时器频率跟随目标帧率 30 / 60 / 120，并在目标变化时重设 | second review R03: no fixed 60 Hz; the timer follows the 30 / 60 / 120 target and is re-armed on change
        self.assertNotRegex(off, r'1\.0 / 60\b')
        self.assertIn('mtl_fps_target()', off)
        self.assertRegex(off, r'f == 30 \? 30 : f == 120 \? 120 : 60')
        self.assertRegex(off, r'if \(hz != cur_hz\).*dispatch_source_set_timer', 're-arm on change')
        self.assertIn('dispatch_get_main_queue()', off)
        self.assertIn('g_work_driver = 2', off)

    def test_work_logs_actual_rate_and_driver(self):
        work = block(FRONT, '- (void)work {')
        self.assertIn('g_work_driver', work); self.assertRegex(work, r'次/秒')


class PresentedCount(unittest.TestCase):
    def test_label_reads_presented_count(self):
        i = FRONT.index('static UIView *attach_fps_hud(UIView *top)')
        hud = FRONT[i:i + 3000]
        self.assertIn('mtl_presented_count()', hud); self.assertNotIn('mtl_present_count()', hud)

    def test_presented_counter_only_on_real_presentation(self):
        i = MTL.index('[dr addPresentedHandler:^(id<MTLDrawable> d) {')
        handler = MTL[i:i + 600]
        self.assertRegex(handler, r'if \(tt > 0\) atomic_fetch_add_explicit\(&g_presented_ok, 1')
        self.assertIn('static _Atomic uint64_t g_presented_ok;', MTL)

    def test_shot_request_reads_counter_atomically(self):
        # 二审 R04：截图请求来自其他线程，不得直接读 g_present_n | second review R04: shot requests come from other threads and must not read g_present_n directly
        shot = block(MTL, 'void dm_gfx_shot_at(')
        self.assertNotRegex(shot, r'\bg_present_n\b'); self.assertIn('mtl_present_count()', shot)

    def test_attempt_counter_atomic_access(self):
        self.assertIn('__atomic_add_fetch(&g_present_n, 1, __ATOMIC_RELAXED)', MTL)
        self.assertIn('return __atomic_load_n(&g_present_n, __ATOMIC_RELAXED)', MTL)
        self.assertEqual(len(re.findall(r'\bg_present_n\+\+', MTL)), 0)



class FpsInHud(unittest.TestCase):
    """用户 2026-10-27：帧数不再是右上角的 UIKit 标签（字体不一致、灰黑底板突兀、在 iPhone Duo 上压着小地图下的地名），改为接在底栏经验行末尾，同字形同色
    User, 2026-10-27: the frame rate is no longer a top-right UIKit label (wrong font, grey-black plate, covered the minimap area name on iPhone Duo); it is appended to the bottom-bar experience line in the same glyphs and color"""
    OVERLAY = (ROOT / 'ios' / 'Sources' / 'DmTouchOverlay.m').read_text(encoding='utf-8')
    GEN = (ROOT / 'ios' / 'tools' / 'gen_hud_v4.py').read_text(encoding='utf-8')

    def test_no_separate_label(self):
        hud = block(FRONT, 'static UIView *attach_fps_hud(UIView *top) {')
        for gone in ('UILabel', 'backgroundColor', 'Palatino', 'addSubview', 'trailingAnchor'):
            self.assertNotIn(gone, hud, gone)
        self.assertIn('g_fps_hud =', hud)

    def test_drawn_on_experience_line(self):
        self.assertIn('stringByAppendingFormat:@" · %d FPS",self.fps', self.OVERLAY)
        self.assertRegex(self.OVERLAY, r'v4text\(lvs,lv\[@"glyphs"\]')
        self.assertIn('_overlay.fps=g_fps_hud', FRONT)

    def test_glyphs_cover_fps(self):
        self.assertRegex(self.GEN, r"glyphs\('lv',\(205,185,140,255\),chars='[^']*F[^']*'\)")
        for ch in 'FPS':
            self.assertRegex(self.GEN, r"glyphs\('lv',[^)]*\),chars='[^']*" + ch)

    def test_setting_name_has_no_position(self):
        row = next(l for l in (ROOT / 'i18n' / 'ui_text.tsv').read_text(encoding='utf-8').splitlines() if l.startswith('dmCustomFpsHud\t'))
        for word in ('右上角', 'top right', 'arriba', 'справа', '오른쪽'):
            self.assertNotIn(word, row.split('\t')[1:7].__str__(), word)


if __name__ == '__main__':
    unittest.main(verbosity=1)
