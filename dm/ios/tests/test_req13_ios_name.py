#!/usr/bin/env python3
"""REQ13 回归：iOS 宿主 App 的显示名是「D2R Mobile Plus」，Bundle ID 与数据容器不变。

读 dm/ios/project.yml（XcodeGen 的输入；生成的 Info.plist 不进仓库）；主 App 目标 DmIOS 的
CFBundleDisplayName 被改回旧名「D2R Mobile」时失败。同时钉住：Bundle ID 仍来自 ${DM_BUNDLE_ID}，
资源文件夹仍是 "D2R Mobile Resources"（显示名变了，文件 App 里的 App 文件夹名跟着变，但容器内路径不变）。
不依赖 PyYAML：只按 project.yml 的两空格目标块切分。

REQ13 regression: the iOS host app's display name is "D2R Mobile Plus", while the Bundle ID and the
data container stay unchanged. Reads dm/ios/project.yml (the XcodeGen input; the generated Info.plist
is not committed). Fails when the DmIOS target is reverted to the old name. Also pins that the Bundle
ID still comes from ${DM_BUNDLE_ID} and the resource folder is still "D2R Mobile Resources".
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))   # dm/ios/tests -> repo root
PROJECT = os.path.join(ROOT, 'dm', 'ios', 'project.yml')


def target_block(text, name):
    """Return the lines of a two-space-indented target block."""
    lines = text.splitlines()
    start = None
    for i, line in enumerate(lines):
        if re.fullmatch(r'  %s:\s*' % re.escape(name), line):
            start = i
            break
    if start is None:
        return None
    end = len(lines)
    for j in range(start + 1, len(lines)):
        if re.match(r'  [A-Za-z0-9_.-]+:\s*(#.*)?$', lines[j]):
            end = j
            break
    return '\n'.join(lines[start:end])


def main():
    text = open(PROJECT, encoding='utf-8').read()
    fails = []

    block = target_block(text, 'DmIOS')
    if block is None:
        fails.append('project.yml 里找不到 targets 下的 DmIOS 目标')
    else:
        m = re.search(r'^\s+CFBundleDisplayName:\s*([^#\n]+?)\s*(?:#.*)?$', block, re.M)
        got = m.group(1).strip() if m else None
        if got != 'D2R Mobile Plus':
            fails.append('DmIOS 的 CFBundleDisplayName 应为 "D2R Mobile Plus"，实际是 %r' % got)
        # Bundle ID 不得改：主 App 仍是 ${DM_BUNDLE_ID}（数据容器跟着它走）
        m = re.search(r'^\s+PRODUCT_BUNDLE_IDENTIFIER:\s*(\S+)', block, re.M)
        if not m or m.group(1) != '${DM_BUNDLE_ID}':
            fails.append('DmIOS 的 PRODUCT_BUNDLE_IDENTIFIER 变了：%r（应仍为 ${DM_BUNDLE_ID}）'
                         % (m.group(1) if m else None))

    # 数据容器：App 内部资源文件夹名不动（显示名只影响文件 App 里显示的那层）
    asset = open(os.path.join(ROOT, 'dm', 'ios', 'Sources', 'asset_dl.m'), encoding='utf-8').read()
    if '"D2R Mobile Resources/data"' not in asset:
        fails.append('asset_dl.m 的资源容器路径不是 "D2R Mobile Resources/data"')
    main_m = open(os.path.join(ROOT, 'dm', 'ios', 'Sources', 'main.m'), encoding='utf-8').read()
    if '"D2R Mobile Resources"' not in main_m:
        fails.append('main.m 不再识别 "D2R Mobile Resources" 目录')

    if fails:
        print('test_req13_ios_name FAIL')
        for f in fails:
            print('  ✗ ' + f)
        return 1
    print('test_req13_ios_name OK：DmIOS 显示名 "D2R Mobile Plus"，Bundle ID 与数据容器未变')
    return 0


if __name__ == '__main__':
    sys.exit(main())
