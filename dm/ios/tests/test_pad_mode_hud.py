"""手柄模式下触屏覆盖层的源码级回归（用户 2026-10-28：进手柄模式后 XP 数字和帧数没了）：
front_ios.m 的 padMode: 不再因手柄模式整层藏 _overlay，而是设 overlay.padMode；DmTouchOverlay 按公共 dm/host/dm_hud_mask.h 决策：
手柄模式 drawRect 不画触屏控件、drawV4 不画底图 / 数字、只画经验行（含帧数）。Android 走同一份 dm_hud_mask.h（pad_android.c）。
Source-level regression for the touch overlay in controller mode (user 2026-10-28: the XP digits and the frame rate vanished in controller mode):
front_ios.m padMode: no longer hides _overlay as a whole in controller mode but sets overlay.padMode; DmTouchOverlay decides per the shared dm/host/dm_hud_mask.h:
in controller mode drawRect draws no touch controls, drawV4 no bar art / numbers, only the experience line (frame rate included). Android uses the same dm_hud_mask.h (pad_android.c)."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FRONT = (ROOT / 'ios' / 'Sources' / 'front_ios.m').read_text(encoding='utf-8')
OVERLAY = (ROOT / 'ios' / 'Sources' / 'DmTouchOverlay.m').read_text(encoding='utf-8')
HEADER = (ROOT / 'ios' / 'Sources' / 'DmTouchOverlay.h').read_text(encoding='utf-8')
MASK = (ROOT / 'host' / 'dm_hud_mask.h').read_text(encoding='utf-8')
_ANDROID = ROOT / 'android' / 'pad_android.c'   # iOS 审计分支不带安卓线（CLAUDE.md §6），没有就跳过那一项 | the iOS audit branch carries no Android line; skip that check when absent
ANDROID = _ANDROID.read_text(encoding='utf-8') if _ANDROID.exists() else None


def block(src, start):
    i = src.index(start); depth = 0; j = src.index('{', i)
    for k in range(j, len(src)):
        depth += src[k] == '{'; depth -= src[k] == '}'
        if depth == 0:
            return src[i:k + 1]
    raise AssertionError('unbalanced: ' + start)


class PadModeHud(unittest.TestCase):
    def test_front_keeps_overlay_in_pad_mode(self):
        pad = re.search(r'- \(void\)padMode:\(BOOL\)pad \{.*?\n', FRONT).group(0)
        self.assertIn('_overlay.hidden=_feMode;', pad); self.assertNotIn('_feMode || pad', pad)
        self.assertIn('_overlay.padMode=pad', pad)
        fe = re.search(r'- \(void\)frontendMode:\(BOOL\)fe \{.*?\n', FRONT).group(0)
        self.assertNotIn('|| _padMode', fe)
        self.assertNotIn('BOOL hide=_feMode || _padMode || dm_padmode_busy()', FRONT)
        self.assertIn('BOOL hide=_feMode || dm_padmode_busy()', FRONT)

    def test_overlay_uses_shared_mask(self):
        self.assertIn('BOOL padMode', HEADER)
        self.assertIn('dm_hud_mask.h', OVERLAY)
        self.assertIn('if (!(dm_hud_draw_mask(self.padMode) & DM_HUD_CONTROLS)) { [self drawV4:c]; return; }', OVERLAY)
        v4 = block(OVERLAY, '- (void)drawV4:(CGContextRef)c {')
        self.assertIn('const int mask=dm_hud_draw_mask(self.padMode)', v4)
        head = v4[:v4.index('return; }') + 9]   # 手柄分支在底图之前 | the controller branch comes before the bar art
        self.assertIn('FPS', head); self.assertIn('v4text(lvs0', head); self.assertNotIn('base_full.png', head)

    def test_mask_table_shared_with_android(self):
        self.assertIn('return pad_mode ? DM_HUD_EXPLINE : DM_HUD_CONTROLS | DM_HUD_BAR | DM_HUD_EXPLINE', MASK)
        if ANDROID is not None:
            self.assertIn('return dm_hud_draw_mask(pad_mode)', ANDROID)


if __name__ == '__main__':
    unittest.main()
