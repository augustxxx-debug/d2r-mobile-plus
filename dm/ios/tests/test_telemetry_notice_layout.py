"""设备与使用统计的告知框 v1（dm/host/dm_telebox.c）与设置开关：主菜单两种面板末尾都挂 30 个控件（边框 15 片 + 标题 + 正文十行 + 副文 + 按键说明 + 两颗按钮），屏外起步、不带锚点、§2.6 三级字号与原版色票；
每条文案六语齐全、字宽按估算不压框、不含平台专属词（§2.8）；开关行在两端都有；宿主 / 外壳 / 构建配置都接上，源码里没有服务地址，设备码只取 IDFV / ANDROID_ID。
The statistics notice v1 and its switch: both main-menu panels end with its 30 widgets, off screen, unanchored, §2.6 sizes and original colour tokens; every line exists in all six languages, fits by estimated width and carries no platform-specific word (§2.8);
the switch row exists on both platforms; host / shells / build settings are wired, no service address in the source, the device code comes only from IDFV / ANDROID_ID."""
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
from test_community_entry import est_width, BTN_W  # noqa: E402

BOX = ['TouchTeleTile%d%d' % (r, c) for r in range(5) for c in range(3)] + ['TouchTeleTitle'] + ['TouchTeleDesc%d' % k for k in range(12)] + ['TouchTeleAgree', 'TouchTeleDecline']
TEXT_W = 2580   # dm_telebox.c TEXT_W


class TelemetryNotice(unittest.TestCase):
    def test_box_widgets(self):
        t = touch_layout._TELE
        for w in BOX: self.assertEqual(t.count('"name": "%s"' % w), 1, w)
        self.assertNotIn('"anchor"', t, 'unanchored: the host places it from the panel rect')
        self.assertEqual(len(re.findall(r'"pointSize": 48', t)), 1); self.assertEqual(len(re.findall(r'"pointSize": 36', t)), 10)
        self.assertGreaterEqual(len(re.findall(r'"pointSize": 28', t)), 2)
        for c in ('$FontColorGoldYellow', '$FontColorLightGold', '$FontColorLightGray', '$ReducedSpacing'): self.assertIn(c, t)
        self.assertNotRegex(t, r'"fontColor": \[', 'colour tokens only')

    def test_on_both_main_menu_panels(self):
        src = (DM / 'ios' / 'tools' / 'touch_layout.py').read_text(encoding='utf-8')
        self.assertEqual(src.count('_COMM + _TELE'), 3, 'pad ribbon (both) and the touch main menu')
        self.assertIn('_COMM_STUB + _TELE_STUB', src)
        self.assertIn("folder='ztouch_tele'", (DM / 'ios' / 'tools' / 'gen_romfs_overlay.py').read_text(encoding='utf-8'))

    def test_strings(self):
        rows = {r[0]: r[1:] for r in dm_i18n.rows()}
        limits = {'dmTeleTitle': (TEXT_W, 48), 'dmTeleSub': (TEXT_W, 28), 'dmTelePad': (TEXT_W, 28), 'dmTeleAgree': (BTN_W, 36), 'dmTeleDecline': (BTN_W, 36)}
        limits.update({'dmTele%d' % k: (TEXT_W, 36) for k in range(10)})
        banned = re.compile(r'MetalFX|Metal\b|iCloud|Apple|TestFlight|Xcode|MetricKit|Dynamic Island|灵动岛|Vulkan|Turnip|Download/|宝塔', re.I)
        for k, (w, pt) in limits.items():
            self.assertIn(k, rows, k)
            for lang, text in zip(('zh', 'en', 'es', 'mx', 'ru', 'ko'), rows[k]):
                self.assertTrue(text, (k, lang)); self.assertNotIn('\n', text, 'one text box per line')
                self.assertLessEqual(est_width(text, pt), w, (k, lang, text)); self.assertNotRegex(text, banned)

    def test_switch_row(self):
        self.assertIn('tel_stats', touch_layout._CUSTOM_KEYS)
        self.assertLess(touch_layout._CUSTOM_KEYS.index('tel_stats'), touch_layout._CUSTOM_KEYS.index('auto_crash'), 'the statistics row sits right above the crash report row')
        host = (DM / 'host' / 'dm_custom.c').read_text(encoding='utf-8')
        self.assertIn('"tel_stats", "auto_crash"', host); self.assertIn('tel_notice', host)

    def test_wiring_and_privacy(self):
        hid = (DM / 'host' / 'hle_hid.c').read_text(encoding='utf-8'); diag = (DM / 'host' / 'diag.c').read_text(encoding='utf-8'); mk = (DM / 'host' / 'Makefile').read_text(encoding='utf-8')
        self.assertIn('dm_telemetry_pump()', hid); self.assertIn('dm_telebox_pump()', hid); self.assertIn('dm_telebox_input_gate(&s->buttons', hid); self.assertIn('dm_telebox_on_uimsg(cat, act, arg)', diag)
        self.assertIn('dm_telemetry.c', mk); self.assertIn('dm_telebox.c', mk)
        ios = (DM / 'ios' / 'Sources' / 'dm_sharelog.m').read_text(encoding='utf-8'); bridge = (DM / 'android' / 'app' / 'src' / 'main' / 'java' / 'com' / 'xiaoboluo' / 'd2r' / 'TelemetryBridge.java').read_text(encoding='utf-8')
        self.assertIn('dm_telemetry_init(&tele_shell)', ios); self.assertIn('identifierForVendor', ios); self.assertIn('"统计上报：", "统计告知："', ios); self.assertIn('ANDROID_ID', bridge)
        for bad in ('getSerial', 'getImei', 'getDeviceId', 'AdvertisingIdClient', 'WifiInfo', 'BluetoothAdapter'): self.assertNotIn(bad, bridge)
        self.assertNotIn('advertisingIdentifier', ios); self.assertNotIn('UIDevice.currentDevice.name', ios[ios.index('tele_device'):ios.index('tele_shell')], 'no device name in the statistics')
        for f in ('host/dm_telemetry.c', 'host/dm_telemetry.h', 'host/dm_telemetry_fields.h', 'host/dm_telebox.c', 'android/telemetry_android.c', 'android/app/src/main/java/com/xiaoboluo/d2r/TelemetryBridge.java', 'android/app/src/main/java/com/xiaoboluo/d2r/TelemetryClient.java'):
            urls = [u for u in re.findall(r'https?://[A-Za-z0-9.\-\[\]:]+', (DM / f).read_text(encoding='utf-8')) if not re.match(r'https?://(127\.0\.0\.1|\[?::1\]?|localhost)', u)]
            self.assertEqual(urls, [], (f, urls))
        self.assertIn('DM_CHANNEL: unknown', (DM / 'ios' / 'project.yml').read_text(encoding='utf-8'))
        swift = (DM / 'ios' / 'Sources' / 'dm_diag_upload.swift').read_text(encoding='utf-8')
        self.assertIn('org.d2rmobile.telemetry.installation.v1', swift); self.assertIn('D2R-TELEMETRY-V1', swift)
        self.assertIn('"telemetry/v1/"', swift)


if __name__ == '__main__':
    unittest.main()
