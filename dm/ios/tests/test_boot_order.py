"""十四审 Q14-01 / Q14-02：iOS 壳要在建游戏视图（front_ios_view → front_game_res 算启动预留、问光追降档 / 内存档位）之前，把本进程内存上限与设置目录交给宿主。
原先这两步在 boot_prepare（场景到前台、host_start 之后），建视图时设置读不到、上限没量，判定被记死。现在 didFinishLaunching 里先调 custom_early_init()，
两处建视图（willConnect、资源下载完就地起宿主）都在它之后；front_game_res 算预留时若设置还没读到要记红（防回退）。纯宿主单测测不到壳的顺序，这里按源码钉住。
Review 14 Q14-01 / Q14-02: the iOS shell must hand the process memory limit and the settings directory to the host before it builds the game view (front_ios_view -> front_game_res computes the startup reserve and asks about the RT drop / memory tier).
Both used to happen in boot_prepare (scene foreground, after host_start); when the view was built the settings were unreadable and the limit unmeasured, so the decisions latched. Now didFinishLaunching calls custom_early_init() first,
both view builders (willConnect, and starting the host in place after the asset download) come after it, and front_game_res logs red if it computes the reserve before the settings were read (regression guard). A host-only unit test cannot see the shell's order, so this pins it in the source."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAIN = (ROOT / 'ios' / 'Sources' / 'main.m').read_text(encoding='utf-8')
FRONT = (ROOT / 'ios' / 'Sources' / 'front_ios.m').read_text(encoding='utf-8')


def body(src, start):
    i = src.index(start); j = src.index('{', i); depth = 0
    for k in range(j, len(src)):
        depth += src[k] == '{'; depth -= src[k] == '}'
        if depth == 0:
            return src[i:k + 1]
    raise AssertionError('unbalanced: ' + start)


class BootOrder(unittest.TestCase):
    def test_early_init_hands_limit_and_settings_dir(self):
        b = body(MAIN, 'static void custom_early_init(void)')
        self.assertIn('dm_mem_measure_set(g_mem_limit_mb, g_mem_phys_mb, g_mem_model)', b)
        self.assertIn('dm_custom_init(', b)
        self.assertIn('NSDocumentDirectory', b)

    def test_did_finish_launching_calls_it_before_views(self):
        b = body(MAIN, '- (BOOL)application:(UIApplication *)a didFinishLaunchingWithOptions:(NSDictionary *)o {')
        self.assertIn('custom_early_init();', b)
        # 场景回调都在 didFinishLaunching 之后；两处建视图本身不得再早于它（不在 main() / 静态构造里建视图）| scene callbacks follow didFinishLaunching; no view is built earlier (not in main() / static constructors)
        self.assertEqual(len(re.findall(r'front_ios_view\(', MAIN)), 3, 'one declaration + willConnect + asset-ready path')
        self.assertNotIn('front_ios_view(', body(MAIN, 'int main(int argc, char *argv[]) {'))

    def test_front_game_res_guards_unread_settings(self):
        b = body(FRONT, 'uint64_t front_game_res(int docked) {')
        self.assertIn('dm_custom_loaded()', b)
        self.assertIn('【红】', b)


if __name__ == '__main__':
    unittest.main()
