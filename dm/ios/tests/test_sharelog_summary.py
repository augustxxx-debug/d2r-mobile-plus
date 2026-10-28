"""分享诊断日志的头部摘要要带上自定义设置读盘行（含显示帧数 fps_hud 等全部键），以及 120 帧档与光追阴影的生效行、回退行、画面切换行（光追 / 原版，2026-10-28 闪烁修复）与 600 帧统计行；iOS 在 perf_block，Mac 在 info.txt，两边都先脱敏再写出（本机规则「新功能上线前适配分享诊断日志」，2026-10-27）。
The diagnostic share header must carry the custom-settings load line (every key, including fps_hud) plus the 120 fps mode and ray-traced-shadow effective, fallback, picture-switch (ray traced / original, 2026-10-28 blink fix) and 600-frame lines; iOS in perf_block, Mac in info.txt, both scrubbed before writing (local rule "adapt the diagnostic share before shipping a feature", 2026-10-27)."""
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
        for k in ('"自定义设置：读盘"', '"图形设置："', '"帧率档：生效"', '"帧率档：出图"', '"光追阴影：机器判定"', '"光追阴影：生效"', '"光追阴影：回退"', '"光追阴影：画面"', '"光追阴影：第"', '"Metal 抓帧层："', '"光追内存：预算"', '"光追内存：第"', '"光追内存：告警"', '"内存 至帧"', '"超分档位："', '"内存档位："', '"光追质量："', '"光追降分辨率："'):
            self.assertIn(k, keys)


    def test_input_mode_line(self):
        # 输入模式（触屏 / 手柄）切换时记一行「输入模式：」，摘要取最后一条（用户 2026-10-28：手柄模式下的问题要能从日志看出当时是哪种模式）
        # A "输入模式：" (input mode) line on every touch / controller switch; the summary takes the last one (user 2026-10-28: controller-mode reports must show the mode)
        keys = re.search(r'static const char \*keys\[\] = \{([^}]*)\}', block(SHARE, 'static void perf_block(')).group(1)
        self.assertIn('"输入模式："', keys)
        # 锁交接丢唤醒自愈（RC1 阻塞项，2026-10-29）：「线程同步：」行进摘要，iOS / Mac 两端 | lost-wakeup recovery in the lock handoff (RC1 blocker, 2026-10-29): the "线程同步：" line is in the summary on both iOS and Mac
        self.assertIn('"线程同步："', keys); self.assertIn('@"线程同步："', MAC)
        front = (ROOT / 'ios' / 'Sources' / 'front_ios.m').read_text(encoding='utf-8')
        pad = block(front, '- (void)padMode:(BOOL)pad {')
        self.assertIn('LOG("输入模式：', pad)
        self.assertIn('LOG("输入模式：', block(front, '- (instancetype)initWithFrame:'))
    def test_fps_and_profile_on_one_line(self):
        # 帧率档与超分档要在摘要同一行看出联动（用户 2026-10-29：120 帧档不给高画质，设置是高画质时生效是均衡）
        # The frame rate and the upscale profile must show their link on one summary line (user 2026-10-29: no Quality at 120 fps; a saved Quality runs as Balanced)
        build = block(SHARE, 'static NSURL *build_file(void) {')
        line = next(l for l in build.splitlines() if 'FPS target=' in l)
        self.assertIn('profileSelected=', line); self.assertIn('profileEffective=', line); self.assertIn('qualityCapped120=', line)

    def test_memory_tier_launch_and_now(self):
        # 内存档位：启动时生效的值与当前设置并列（用户 2026-10-29：局中调低不还内存，渲染档在启动时定预留）；预留行进摘要键
        # Memory tier: launch values next to the current settings (user 2026-10-29: lowering mid-run returns no memory, the render level fixes the reserve at launch); the reserve line is a summary key
        build = block(SHARE, 'static NSURL *build_file(void) {')
        line = next(l for l in build.splitlines() if 'Memory tier=' in l)
        self.assertIn('atLaunch', line); self.assertIn('now renderRes', line)
        keys = re.search(r'static const char \*keys\[\] = \{([^}]*)\}', block(SHARE, 'static void perf_block(')).group(1)
        self.assertIn('"内存档位预留："', keys)


class MacSummary(unittest.TestCase):
    def test_info_has_settings_lines(self):
        share = block(MAC, 'static NSString *share_logs(NSString *to) {')
        for k in ('自定义设置：读盘', '图形设置：', '帧率档：生效', '帧率档：出图', '光追阴影：机器判定', '光追阴影：生效', '光追阴影：回退', '光追阴影：画面', '光追阴影：第', 'Metal 抓帧层：', '光追内存：预算', '光追内存：第', '光追内存：告警', '内存 至帧', '超分档位：', '内存档位：', '光追质量：'):
            self.assertIn(k, share)

    def test_summary_goes_through_scrub(self):
        share = block(MAC, 'static NSString *share_logs(NSString *to) {')
        i = share.index('自定义设置：读盘'); j = share.index('info.txt')
        self.assertLess(i, j, 'summary is built before info.txt is written')
        self.assertRegex(share, r'scrub\(\[info dataUsingEncoding')



class CrashFence(unittest.TestCase):
    """崩溃围栏：系统崩溃日志附在导出末尾、先于脱敏；摘要有「崩溃围栏：」行与崩溃日志摘要行（iOS / Mac 两端）。
    Crash fence: the system crash log is appended at the end of the export, before scrubbing; the summary has the crash-fence line and the crash-log summary line (both iOS and Mac)."""
    def test_ios_crash_section_before_scrub(self):
        build = block(SHARE, 'static NSURL *build_file(void) {')
        self.assertIn('dm_crashlog_section(', build)
        i = build.index('[out appendData:crash]'); j = build.index('dm_diagnostic_scrub_names(out')
        self.assertLess(i, j, 'crash section is appended before the export is scrubbed')
        self.assertLess(build.index('part(@"上一局日志 boot.previous.txt"'), i, 'crash section is at the end')
        self.assertIn('[h appendFormat:@"%@\\n", crash_sum]', build)
        keys = re.search(r'static const char \*keys\[\] = \{([^}]*)\}', block(SHARE, 'static void perf_block(')).group(1)
        self.assertIn('"崩溃围栏："', keys)

    def test_mac_crash_file_scrubbed(self):
        share = block(MAC, 'static NSString *share_logs(NSString *to) {')
        self.assertIn('dm_crashlog_newest(', share)
        self.assertRegex(share, r'\[scrub\(crash\) writeToFile:\[dir stringByAppendingPathComponent:@"crash\.txt"\]')
        self.assertIn('@"崩溃围栏："', share)
        self.assertIn('crash_sum', share)


if __name__ == '__main__':
    unittest.main(verbosity=1)
