#!/usr/bin/env python3
"""B066 守卫与右摇杆跳组落点的回归测试（审计 P2-1 / P2-2）。

dm_padmode.c 依赖整个宿主，不能直接链接；照 test_mac_features.py 的办法，从源码里取出这两处判断，
用 clang 编成小函数跑真值表。守卫被删（局外又会上到难度行）或落点改回 to = 0（局外焦点落在死行）时失败。
Regression test for the B066 guard and the right-stick group jump (audit P2-1 / P2-2).

dm_padmode.c depends on the whole host and cannot be linked directly; like test_mac_features.py, take the two
decisions out of the source, compile them as small functions with clang and run a truth table. It fails when the
guard is removed (outside a run Up enters the difficulty row again) or the landing goes back to to = 0 (the focus
lands on the dead row outside a run).
"""
import re
import subprocess
import tempfile
from pathlib import Path

DM = Path(__file__).resolve().parents[2]
SRC = (DM / 'host/dm_padmode.c').read_text()

# B066：局外（shown = 0）从入口钮（st[1]）按上不能进难度行（st[0]）；局内（shown != 0）照常进。
# B066: outside a run (shown = 0) Up from the entry button (st[1]) must not enter the difficulty row (st[0]);
# inside a run (shown != 0) it does.
m = re.search(r'\(ev & DM_BTN_UP\) && si > 0 && st\[si - 1\] && (\([^()]*\))\)', SRC)
assert m, 'B066 上移守卫不见了：dm_padmode.c 的 UP 分支里没有 (… || shown) 条件'
guard = m.group(1)

# 右摇杆上跳没有折叠钮时的落点：局外跳过难度行（st[0]），落第一条可操作的行；局内难度行可调，照常落 0。
# Right-stick jump up with no fold button: outside a run skip the difficulty row (st[0]) and land on the first
# actionable stop; inside a run the difficulty row is adjustable, so land on 0 as usual.
m = re.search(r'(if \(si >= 0 && rs < 0 && to < 0\) \{[^}]*\})', SRC)
assert m, '右摇杆跳组落点不见了：dm_padmode.c 里没有 rs < 0 && to < 0 的落点处理'
land = m.group(1)

source = f'''#include <assert.h>
#include <stdio.h>
static int guard(int si, int shown) {{ return {guard}; }}
static int land(int si, int rs, int ns, int shown, const unsigned long long *st) {{ int to = -1; {land} return to; }}
int main(void) {{
    /* 上移守卫：局外 si = 1 不进难度行；局内可以；别的停靠点照常往上走 */
    assert(guard(1, 0) == 0);
    assert(guard(1, 1) == 1);
    assert(guard(2, 0) == 1);
    /* 右摇杆上跳：局外落入口钮（1），不落难度行（0）；局内照常落 0 */
    const unsigned long long all[4] = {{ 1, 1, 1, 1 }};
    assert(land(1, -1, 4, 0, all) == 1);
    assert(land(2, -1, 4, 0, all) == 1);
    assert(land(1, -1, 4, 1, all) == 0);
    /* 入口钮不在（控件没找到）：落到下一个可操作停靠点 */
    const unsigned long long gap[4] = {{ 1, 0, 1, 1 }};
    assert(land(1, -1, 4, 0, gap) == 2);
    /* 只有难度行一条：没有别的可落，只能回去 */
    assert(land(1, -1, 1, 0, all) == 0);
    puts("PASS t_padmode_focus: B066 guard holds; right-stick upward jump never lands on the out-of-run difficulty row");
    return 0;
}}
'''

with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / 'focus.c'
    exe = Path(tmp) / 'focus'
    src.write_text(source)
    subprocess.run(['clang', '-O1', '-Wall', '-Werror', str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
