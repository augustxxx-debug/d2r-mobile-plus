#!/usr/bin/env python3
"""B099 / B100 回归：手柄模式佣兵页。

B099（原版规则，只补说明）：未鉴定物品放不上佣兵格是原版规则——原版「佣兵能不能穿这件」main+0x3bea44 在判类型之前
（0x3beaa8–0x3beab8）先看物品数据 +0x18：(标志 & 0x110) != 0x10 直接返回 0，原版四格同样拦。规则不改；手柄模式拿着未鉴定物品
在佣兵格上按 A 时弹一句「佣兵不能穿未鉴定的物品」。
  核：有 main.orig.bin 时那五条指令逐字对得上（原版规则的证据；没有就跳过这一项）；dm_held_unid 用 dm_item_flags_unid；
  ui_target_pump 里那一段只在手柄模式、佣兵面板、焦点格属于佣兵面板、手上未鉴定时收 A 并提示；提示文字六语齐。
B100 ①：副手格按「上」：布局 NAV 写的是 up = 头盔格，原版不认（头盔格不整个在副手格上面），落到顶部页签条。宿主接这一下，
  把光标放到头盔格中心。核：NAV 仍写 up = head；按真布局（有 RomFS 时 merc_slots.rects）副手→头盔是「原版弃」的那种、躯干→头盔不是；
  t_pad_merc 用真矩形算的落点在头盔格里；宿主那段只接 slot_left_arm 的「上」、核命中再放。
B100 ②：手上拿着物品时页签条上的 A 原版直接换页、物品被带走；宿主收下不交。核：判定走 dm_pad_tab_a_take，页签条按 UISwitcherTabs 找、
  按指针核焦点控件在它下面，手上有无走 dm_held_size；ui_pad_merc_pump 由 ui_panel_state_pump 每帧调。
回退任一处修复（删调用、放宽条件、换掉提示键）这里就红。
B099 / B100 regression: the controller mercenary page. B099 is an original rule (main+0x3bea44 rejects unidentified items before any type test, for the
original four cells too); the rule stays and A on a mercenary cell holding an unidentified item shows a notice. B100 (1): Up on the off-hand cell -- the
layout's up = helm is dropped by the game (the helm is not wholly above), so the host moves the cursor to the helm cell. B100 (2): A on the tab bar with
an item in hand switches the page in the game; the host takes it. Reverting any fix turns this test red.
"""
import os
import re
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
DM = HERE.parents[1]
sys.path.insert(0, str(DM / 'ios' / 'tools'))
CUR = (DM / 'host' / 'dm_cursor.c').read_text(encoding='utf-8')
ITEM = (DM / 'host' / 'dm_cursor_item.c').read_text(encoding='utf-8')
MAIN = Path(os.environ.get('DM_MAIN_ORIG', HERE / 'build' / 'main.orig.bin'))
ROMFS = os.environ.get('DM_ROMFS_SRC') or os.path.join(os.environ.get('DM_ROOT', os.path.expanduser('~/dm-work')), 'data/switch/3.3.94012/Update/RomFS')


def code(s):
    return re.sub(r'//[^\n]*', '', re.sub(r'/\*.*?\*/', '', s, flags=re.S))


def body(src, name):
    m = re.search(r'\n(?:static\s+)?(?:void|int|uint64_t)\s+%s\([^;{]*\)\s*\{' % re.escape(name), src)
    if not m: return None
    i, d = m.end() - 1, 0
    for j in range(i, len(src)):
        d += src[j] == '{'; d -= src[j] == '}'
        if d == 0: return src[i:j + 1]
    return None


def t_pad_merc():
    out = Path(tempfile.gettempdir()) / ('t_pad_merc_%d' % os.getpid())
    subprocess.run(['clang', '-O1', '-Wall', '-Werror', '-I', str(DM / 'host'), '-o', str(out), str(HERE / 't_pad_merc.c')], check=True)
    return out


class B099(unittest.TestCase):
    def test_original_rule_in_binary(self):
        if not MAIN.exists(): self.skipTest('没有 %s' % MAIN)
        d = MAIN.read_bytes()
        words = [struct.unpack_from('<I', d, o)[0] for o in range(0x3beaa8, 0x3beabc, 4)]
        # ldr w8,[x8,#0x18] / mov w9,#0x110 / and w8,w8,w9 / cmp w8,#0x10 / b.ne 0x3bec7c（返回 0）| ... / b.ne to "return 0"
        self.assertEqual(words, [0xb9401908, 0x52802209, 0x0a090108, 0x7100411f, 0x54000e21])

    def test_held_unid(self):
        b = code(body(ITEM, 'dm_held_unid') or '')
        self.assertIn('dm_item_flags_unid(*(uint32_t *)(d + 0x18))', b)
        self.assertIn('+ 0x40', b)   # 手上那件 = 玩家物品栏 +0x40 | held = player inventory +0x40

    def test_notice_block(self):
        b = code(body(CUR, 'ui_target_pump') or '')
        m = re.search(r'dm_held_unid\(\)[^;]*dm_padmode_page_keys\(DM_BTN_A\)[^;]*\)\)\s*\{[^}]*dmMercNoUnid', b)
        self.assertTrue(m, 'ui_target_pump 里没有「拿着未鉴定物品在佣兵格上按 A → 提示」那一段')
        cond = b[:m.end()][b[:m.end()].rfind('uint64_t mp'):]
        for need in ('dm_padmode_current()', '"HirelingInventoryPanel"', 'ui_has(mp, hov, 0)', '"slot_"'):
            self.assertIn(need, cond, need)

    def test_text_six_languages(self):
        import dm_i18n
        row = {r[0]: r for r in dm_i18n.rows()}['dmMercNoUnid']
        for lang, t in zip(('中文', 'English', 'Español', 'Русский', '한국어'), (row[1], row[2], row[3], row[5], row[6])): self.assertTrue(t.strip(), lang)
        self.assertIn('未鉴定', row[1]); self.assertRegex(row[2].lower(), r'unidentified')
        self.assertIn('"dmMercNoUnid"', (DM / 'host' / 'dm_i18n_tab.h').read_text(encoding='utf-8'))


class B100(unittest.TestCase):
    def test_nav_still_says_head(self):
        import merc_slots
        self.assertEqual(merc_slots.NAV['left_arm'].get('up'), 'head')

    def test_real_layout_jump(self):
        if not os.path.exists(os.path.join(ROMFS, 'data', 'build_manifest.psv')): self.skipTest('没有 RomFS（%s）' % ROMFS)
        import merc_slots, touch_layout
        R = merc_slots.rects(touch_layout.read_layout(ROMFS, merc_slots.PLAYER)[1])
        exe = t_pad_merc()
        def run(a, h, k, cx, cy):
            o = subprocess.run([str(exe)] + [str(v) for v in a + h] + [str(k), str(cx), str(cy)], check=True, capture_output=True, text=True).stdout.split()
            return int(o[0]), int(o[1]), int(o[2])
        la, hd, to = R['left_arm'], R['head'], R['torso']
        for H, k in ((1080, 1080 / 2160), (2006, 2006 / 2160), (1179, 1179 / 2160)):   # 16:9、Duo 内屏、iPhone 档 | 16:9, Duo inner screen, iPhone class
            # 光标在副手格里的几个点（中心、偏上、偏左下）| cursor at a few points inside the off-hand cell
            ox, oy = 37, 11   # 面板在屏上的偏移随便取：落点只看副手格左上角（宿主拿命中函数量出来的） | any panel offset on screen: the landing only depends on the off-hand top-left (measured by the host with the hit test)
            tx, ty, dropped = run(la, hd, k, ox + int(la[0] * k), oy + int(la[1] * k))
            self.assertEqual(dropped, 1, '真布局里副手→头盔不再是原版弃的那种了，宿主那段可以拿掉')
            cx, cy = ox + (hd[0] + hd[2] / 2) * k, oy + (hd[1] + hd[3] / 2) * k
            self.assertLessEqual(abs(tx - cx), 2, (H, tx, cx)); self.assertLessEqual(abs(ty - cy), 2, (H, ty, cy))   # 落在头盔格中心 | lands on the helm centre
            self.assertEqual(run(to, hd, k, 0, 0)[2], 0)   # 躯干→头盔原版自己认，宿主不接 | torso → helm is the game's own
        # NAV 里所有「上」：原版弃的那些（按判据）必须正好是宿主表 k_up 接的那些，落点都在目标格中心（p4_upedges 七条实测） | every NAV "up": the dropped ones must be exactly the host table k_up
        tbl = set(re.findall(r'\{ "slot_(\w+)", "slot_(\w+)" \}', re.search(r'k_up\[\] = \{(.*?)\};', CUR).group(1)))
        dropped = set()
        for s_, nav in merc_slots.NAV.items():
            t_ = nav.get('up')
            if not t_: continue
            a, h = R[s_], R[t_]
            tx, ty, d = run(a, h, 0.5, int(a[0] * 0.5), int(a[1] * 0.5))
            if d:
                dropped.add((s_, t_)); self.assertLessEqual(abs(tx - (h[0] + h[2] / 2) * 0.5), 2); self.assertLessEqual(abs(ty - (h[1] + h[3] / 2) * 0.5), 2)
        self.assertEqual(dropped, {('left_arm', 'head'), ('right_hand', 'torso')})
        self.assertEqual(tbl, dropped, '宿主 k_up 与「原版会弃的 NAV 上边」不一致')

    def test_pump(self):
        b = code(body(CUR, 'ui_pad_merc_pump') or '')
        self.assertTrue(b, '没有 ui_pad_merc_pump')
        self.assertIn('dm_padmode_current()', b)
        self.assertRegex(b, r'if \(!strcmp\(hn, k_up\[i\]\.from\)\) e = i;'); self.assertIn('if (e < 0 || !root_panel_shown(root, "HirelingInventoryPanel")) return;', b)
        self.assertIn('ui_find(mp, k_up[e].to, 0)', b)
        self.assertIn('dm_pad_up_dropped(a, h)', b)
        self.assertIn('dm_padmode_page_keys(DM_BTN_UP)', b)
        self.assertRegex(b, r'dm_pad_slot_jump\(x0, y0, a, h, k, &tx, &ty\);\s*if \(!PM_HIT\(dst, tx, ty\)\)[^\n]*return; \}[^\n]*\n\s*if \(!\(dm_padmode_page_keys\(DM_BTN_UP\)')   # 落点核过才收「上」 | Up is taken only after the spot is checked
        self.assertRegex(b, r'while \(x0 > 1 && PM_HIT\(hov, x0 - 2, cy\)\) x0 -= 2; while \(y0 > 1 && PM_HIT\(hov, cx, y0 - 2\)\) y0 -= 2;')
        self.assertIn('dm_ui_hover_now(tx, ty)', b)
        self.assertIn('int held = dm_held_size(NULL, NULL)', b)
        self.assertRegex(b, r'dm_pad_tab_a_take\(1, held, tabs && \*\(uint8_t \*\)\(tabs \+ 0x50\) && ui_has\(tabs, hov, 0\), 0\)')
        self.assertIn('ui_find(root, "UISwitcherTabs", 0)', b)
        self.assertRegex(b, r'dm_pad_tab_a_take\([^;]*\)\)\s*\{\s*if \(dm_padmode_page_keys\(DM_BTN_A\) & DM_BTN_A\)')
        self.assertIn('modal_any(root, 1, NULL)', b)
        st = code(body(CUR, 'ui_panel_state_pump') or '')
        self.assertIn('ui_pad_merc_pump(main_module(), root);', st)

    def test_pure(self):
        subprocess.run([str(t_pad_merc())], check=True)


if __name__ == '__main__':
    unittest.main()
