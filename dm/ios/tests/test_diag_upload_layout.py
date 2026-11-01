"""「上传诊断报告」提示框（dm/host/dm_diagup.c）：暂停菜单两份手柄 / 通用布局末尾都挂上 25 个控件（边框 15 片 + 标题 + 三行字 + 六颗按钮），屏外起步、不带锚点、按 §2.6 三级字号与原版色票；
Mac App 版（没有「分享诊断日志」行）不挂；宿主用到的每个表键在 ui_text.tsv 里六套齐全、字宽按估算不压框；宿主 / 外壳 / 构建配置都接上，且源码里没有服务地址。
The "Upload diagnostic report" box (dm/host/dm_diagup.c): both pause-menu layouts end with its 25 widgets (15 frame tiles + title + three lines + six buttons), off screen, unanchored, §2.6 sizes and original colour tokens;
the Mac App variant (no Share Diagnostics row) does not carry it; every table key the host uses exists in all six sets of ui_text.tsv and fits by estimated width; host / shells / build settings are wired and no service address is in the source."""
import os
import re
import sys
import unittest
from pathlib import Path

DM = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(DM / 'ios' / 'tools')); sys.path.insert(0, str(Path(__file__).resolve().parent))
os.environ.pop('MOVIE_FFMPEG', None); os.environ.pop('DM_OVERLAY_MAC', None)
import touch_layout  # noqa: E402
import dm_i18n  # noqa: E402
from test_community_entry import fake_pause, est_width, BOX_W, BTN_W  # noqa: E402

BOX = ['TouchDiagTile%d%d' % (r, c) for r in range(5) for c in range(3)] + ['TouchDiagTitle', 'TouchDiagDesc0', 'TouchDiagDesc1', 'TouchDiagDesc2'] + \
      ['TouchDiag' + b for b in ('Upload', 'Retry', 'Export', 'Copy', 'Cancel', 'Close', 'Enable')]
HOST = (DM / 'host' / 'dm_diagup.c').read_text(encoding='utf-8')


class DiagUploadLayout(unittest.TestCase):
    def test_pause_layouts_carry_the_box(self):
        for garden in (False, True):
            out = touch_layout.pad_pause(fake_pause(garden), garden, share=True)
            for w in BOX: self.assertEqual(out.count('"name": "%s"' % w), 1, (garden, w))
            body = out[out.index('TouchDiagTile00'):]
            self.assertNotIn('"anchor"', body, 'unanchored: the host places it from the panel rect')
            self.assertNotIn('"x": 9', re.sub(r'"x": 99999', '', body), 'starts off screen')
            for b in BOX[19:]: self.assertIn('"onClickMessage": "PanelManager:ClosePanel:%s"' % b, out)   # 空操作消息，宿主当信号收 | no-op messages the host takes as signals
            self.assertGreater(out.index('TouchDiagTile00'), out.index('TouchCommTile00'), 'drawn above the community box')
            self.assertIn('"onClickMessage": "PanelManager:ClosePanel:TouchShareLog"', out, 'the Share Diagnostics row stays the entry')
            out_mac = touch_layout.pad_pause(fake_pause(garden), garden, share=False)
            self.assertNotIn('TouchDiag', out_mac, 'no Share Diagnostics row (Mac App) → no box')

    def test_typography(self):   # §2.6：标题 48 GoldYellow、正文两行 36 LightGold、副文 28 LightGray、字距 ReducedSpacing | §2.6 levels
        box = touch_layout._DIAG
        for name, size, colour in (('TouchDiagTitle', 48, 'GoldYellow'), ('TouchDiagDesc0', 36, 'LightGold'), ('TouchDiagDesc1', 36, 'LightGold'), ('TouchDiagDesc2', 28, 'LightGray')):
            m = re.search(r'"name": "%s".*?"pointSize": (\d+), "fontColor": "\$FontColor(\w+)".*?"spacing": "\$ReducedSpacing"' % name, box)
            self.assertTrue(m, name); self.assertEqual((int(m.group(1)), m.group(2)), (size, colour), name)
        self.assertIn('Ztouch_diag/tile42', box, 'own frame slices')
        gen = (DM / 'ios' / 'tools' / 'gen_romfs_overlay.py').read_text(encoding='utf-8')
        self.assertIn("folder='ztouch_diag',prefix='diag_'", gen)

    def test_every_host_key_is_translated_and_fits(self):
        rows = {r[0]: r[1:] for r in dm_i18n.rows()}
        keys = set(re.findall(r'"(dmDiag\w+)"', HOST)) | set(re.findall(r'"@(dmDiag\w+)"', HOST)) | set(re.findall(r'@(dmDiag\w+)', touch_layout._DIAG))
        self.assertGreater(len(keys), 50)
        sizes = {'Title': 48}
        for k in sorted(keys):
            self.assertIn(k, rows, k)
            zh, en, es, mx, ru, ko = rows[k][:6]
            for lang, text in (('zh', zh), ('en', en), ('es', es), ('ru', ru), ('ko', ko)):
                self.assertTrue(text, (k, lang)); self.assertNotIn('\\n', text, (k, lang))
                shown = text.replace('%s', 'x' * 32 if 'ReportId' in k else '9999' if k in ('dmDiagWait', 'dmDiagKeepDays') else 'x' * 24)
                limit = BTN_W if k in ('dmDiagUpload', 'dmDiagRetry', 'dmDiagExport', 'dmDiagCopy', 'dmDiagCancel', 'dmDiagClose') else BOX_W
                pt = 48 if k.endswith('Title') else 28 if (k.endswith('Hint') or k.startswith('dmDiagPad') or k in ('dmDiagCopied', 'dmDiagCopyFail')) else 36
                if limit == BTN_W: pt = 36
                w = est_width(shown, pt)
                if limit == BTN_W and lang in ('en', 'es'): w = len(shown) * pt   # 按钮字是小型大写，拉丁字母按 1.0 个字号估（Mac 截图：「Export locally」压到按钮两端） | button text is small caps: Latin at 1.0 em (Mac shot: "Export locally" hit the button ends)
                self.assertLessEqual(w, limit, (k, lang, text))
            self.assertEqual(zh.count('%s'), en.count('%s'), k)

    def test_wiring_and_no_address_in_source(self):
        diag = (DM / 'host' / 'diag.c').read_text(encoding='utf-8'); hid = (DM / 'host' / 'hle_hid.c').read_text(encoding='utf-8')
        self.assertIn('dm_diagup_open(1)', diag); self.assertIn('dm_diagup_on_uimsg(cat, act, arg)', diag); self.assertIn('dm_diagup_pump()', hid)
        self.assertIn('dm_diagup.c', (DM / 'host' / 'Makefile').read_text(encoding='utf-8'))
        ios = (DM / 'ios' / 'Sources' / 'dm_sharelog.m').read_text(encoding='utf-8')
        self.assertIn('dm_diagup_init(&diag_shell)', ios); self.assertIn('DMDiagnosticUploadOrigin', ios); self.assertIn('"诊断上传："', ios)
        self.assertIn('dm_diagnostic_scrub_upload(build_data(', ios, 'uploads take the extra scrub pass')
        yml = (DM / 'ios' / 'project.yml').read_text(encoding='utf-8')
        self.assertIn('DMDiagnosticUploadOrigin: $(DM_DIAGNOSTIC_ORIGIN)', yml); self.assertIn('DM_DIAGNOSTIC_ORIGIN: ""', yml)
        for f in ('ios/Sources/dm_diag_upload.swift', 'ios/Sources/dm_sharelog.m', 'host/dm_diagup.c', 'host/diagup_mac.m', 'ios/project.yml'):
            src = (DM / f).read_text(encoding='utf-8')
            urls = [u for u in re.findall(r'https?://[A-Za-z0-9.\-\[\]:]+', src) if not re.match(r'https?://(127\.0\.0\.1|\[?::1\]?|localhost)', u)]
            self.assertEqual(urls, [], (f, urls))   # 地址只能来自构建配置 / 环境变量 | addresses only from build settings / environment
        swift = (DM / 'ios' / 'Sources' / 'dm_diag_upload.swift').read_text(encoding='utf-8')
        self.assertIn('org.d2rmobile.diagnostics.installation.v1', swift); self.assertNotIn('org.d2rmobile.lobby', swift, 'separate from the co-op identity')
        self.assertIn('kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly', swift); self.assertIn('kSecAttrSynchronizable as String: false', swift)
        self.assertNotIn('Authorization', swift, 'no reader / operator credential on the client')


if __name__ == '__main__':
    unittest.main()
