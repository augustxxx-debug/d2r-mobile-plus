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
        for k in ('"自定义设置：读盘"', '"图形设置："', '"帧率档：生效"', '"帧率档：出图"', '"光追阴影：机器判定"', '"光追阴影：生效"', '"光追阴影：回退"', '"光追阴影：画面"', '"光追阴影：第"'):
            self.assertIn(k, keys)


    def test_input_mode_line(self):
        # 输入模式（触屏 / 手柄）切换时记一行「输入模式：」，摘要取最后一条（用户 2026-10-28：手柄模式下的问题要能从日志看出当时是哪种模式）
        # A "输入模式：" (input mode) line on every touch / controller switch; the summary takes the last one (user 2026-10-28: controller-mode reports must show the mode)
        keys = re.search(r'static const char \*keys\[\] = \{([^}]*)\}', block(SHARE, 'static void perf_block(')).group(1)
        self.assertIn('"输入模式："', keys)
        front = (ROOT / 'ios' / 'Sources' / 'front_ios.m').read_text(encoding='utf-8')
        pad = block(front, '- (void)padMode:(BOOL)pad {')
        self.assertIn('LOG("输入模式：', pad)
        self.assertIn('LOG("输入模式：', block(front, '- (instancetype)initWithFrame:'))

class MacSummary(unittest.TestCase):
    def test_info_has_settings_lines(self):
        share = block(MAC, 'static NSString *share_logs(NSString *to) {')
        for k in ('自定义设置：读盘', '图形设置：', '帧率档：生效', '帧率档：出图', '光追阴影：机器判定', '光追阴影：生效', '光追阴影：回退', '光追阴影：画面', '光追阴影：第'):
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
