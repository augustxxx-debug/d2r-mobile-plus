#!/usr/bin/env python3
"""B246：正式包清单地址闸门（dm/android/tools/check_release_urls.py）与出包接线（shell_apk.sh）。
用假 APK（zip 里放带字符串的假 dex）覆盖：没有地址 / 只有库链接与正则字面量 → 不通过；有地址 → 通过；--expect 逐字比对；读不了 → 2。
B246: the release manifest-address gate and its packaging wiring, with fake APKs (a zip holding a fake dex with strings).
python3 -I dm/android/tests/test_release_urls.py"""
import os
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, '..', 'tools', 'check_release_urls.py')
SHELL = os.path.join(HERE, '..', 'tools', 'shell_apk.sh')
LIB = (b'\x00Please follow/upvote https://youtrack.jetbrains.com/issue/KT-55980\x00https://\x00'
       b'&https://[A-Za-z0-9.-]+(:[0-9]{1,5})?/?\x00DM_UPDATE_URLS\x00')
URLS = 'https://dl.example.org/d2r/v1/latest.json https://raw.example.net/o/r/main/latest.json'


def apk(d, name, dex):
    p = os.path.join(d, name)
    with zipfile.ZipFile(p, 'w') as z:
        z.writestr('classes.dex', b'dex\n035\x00' + dex)
        z.writestr('AndroidManifest.xml', b'x')
    return p


def run(*a):
    r = subprocess.run([sys.executable, '-I', TOOL, *a], capture_output=True, text=True)
    return r.returncode, r.stdout


def main():
    fails = 0
    def check(cond, what):
        nonlocal fails
        print(('ok   ' if cond else 'FAIL ') + what); fails += not cond
    with tempfile.TemporaryDirectory() as d:
        none = apk(d, 'none.apk', LIB)                                        # 1.1.0 / 1.1.1 的样子 | how 1.1.0 / 1.1.1 look
        good = apk(d, 'good.apk', LIB + b'\x00' + URLS.encode() + b'\x00')
        rc, out = run(none); check(rc == 1 and '没有更新清单地址' in out, '没给地址（只有库链接 / 正则字面量）→ 不通过')
        rc, out = run(good); check(rc == 0 and 'dl.example.org raw.example.net' in out, '有地址 → 通过并列出主机')
        rc, _ = run(good, '--expect', URLS); check(rc == 0, '--expect 逐字一致 → 通过')
        rc, _ = run(good, '--expect', '  ' + URLS.replace(' ', '   ') + ' '); check(rc == 0, '--expect 只差空白 → 通过')
        rc, out = run(good, '--expect', 'https://other.example.com/latest.json'); check(rc == 1 and '不一致' in out, '--expect 不一致 → 不通过')
        bad = os.path.join(d, 'bad.apk'); open(bad, 'wb').write(b'not a zip')
        rc, _ = run(bad); check(rc == 2, '读不了 → 2')
        nodex = os.path.join(d, 'nodex.apk')
        with zipfile.ZipFile(nodex, 'w') as z: z.writestr('a.txt', 'x')
        rc, _ = run(nodex); check(rc == 2, '没有 dex → 2')
    sh = open(SHELL, encoding='utf-8').read()
    check('check_release_urls.py' in sh and '正式包清单地址闸门不通过' in sh, 'shell_apk.sh 正式包跑闸门且失败即停')
    check('die "正式包没给 DM_UPDATE_URLS' in sh and 'DM_ALLOW_NO_UPDATE_URLS' in sh, 'shell_apk.sh 正式包没给地址先停，只有显式开关能放行')
    print('test_release_urls: %s' % ('PASS' if not fails else '%d FAIL' % fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
