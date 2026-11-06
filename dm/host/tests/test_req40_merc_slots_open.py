#!/usr/bin/env python3
"""REQ40 回归：佣兵项链 / 戒指 ×2 / 腰带 / 鞋 / 手套六格对所有角色缺省可用。

两条证据：
1. 源码闸：dm_rift.c 里不再有旧门槛（R.best >= 10 / DM_TEST_MERC_SLOTS），
   dm_rift_merc_unlocked() 恒返回 1，服务端放行门 svc_merc_slot 不再按解锁状态拒绝；
   merc_slots.py 不再生成暗红盖板，i18n 表里也没有「秘境 10 层解锁」提示。
2. 运行闸：编译并运行 t_req40_merc_slots.c（直接包含真的 dm_rift.c），
   在 R 清零（新角色、普通难度、0 层秘境）且不设测试开关时断言解锁函数返回 1。

把旧门槛放回去（任一函数里恢复 R.best >= 10）会让源码闸失败；恢复后运行闸也失败。
REQ40 regression: the mercenary amulet / two rings / belt / boots / gloves slots are available to
every character by default. Source gate: the old R.best >= 10 / DM_TEST_MERC_SLOTS threshold is gone,
dm_rift_merc_unlocked() always returns 1, svc_merc_slot no longer rejects by unlock state,
merc_slots.py no longer emits the dark red covers, and the i18n table has no "unlock at rift level 10"
hint. Runtime gate: compiles and runs t_req40_merc_slots.c (which includes the real dm_rift.c) with R
zeroed (new character, Normal difficulty, 0 rift levels) and no test switch, asserting unlocked == 1.
Restoring the old threshold makes the source gate (and the runtime gate) fail.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))   # dm/host/tests -> repo root
RIFT = os.path.join(ROOT, 'dm', 'host', 'dm_rift.c')
MERC_SLOTS = os.path.join(ROOT, 'dm', 'ios', 'tools', 'merc_slots.py')
I18N = os.path.join(ROOT, 'dm', 'i18n', 'ui_text.tsv')
C_TEST = os.path.join(HERE, 't_req40_merc_slots.c')
HLE_OS = os.path.join(ROOT, 'dm', 'host', 'hle_os.c')


def function_body(source, name):
    """Return the text of a top-level C function definition, braces balanced."""
    m = re.search(r'\b%s\s*\([^;{]*\)\s*\{' % re.escape(name), source)
    if not m:
        return None
    depth, i = 0, m.end() - 1
    while i < len(source):
        if source[i] == '{':
            depth += 1
        elif source[i] == '}':
            depth -= 1
            if depth == 0:
                return source[m.start():i + 1]
        i += 1
    return None


def source_checks(fails):
    text = open(RIFT, encoding='utf-8').read()
    if re.search(r'R\.best\s*>=\s*10', text):
        fails.append('dm_rift.c 里仍有旧门槛 R.best >= 10')
    if 'DM_TEST_MERC_SLOTS' in text:
        fails.append('dm_rift.c 里仍有 DM_TEST_MERC_SLOTS 解锁开关')
    body = function_body(text, 'dm_rift_merc_unlocked')
    if not body or not re.search(r'return\s+1\s*;', body):
        fails.append('dm_rift_merc_unlocked() 不再无条件返回 1')
    body = function_body(text, 'svc_merc_slot')
    if not body:
        fails.append('dm_rift.c 里找不到 svc_merc_slot()')
    elif 'unlocked' in body or re.search(r'R\.best', body):
        fails.append('svc_merc_slot() 仍按解锁状态 / 秘境层数拒绝')
    body = function_body(text, 'svc_merc_slot_cl')
    if not body:
        fails.append('dm_rift.c 里找不到 svc_merc_slot_cl()')
    elif 'dm_rift_merc_unlocked' in body or re.search(r'R\.best', body):
        fails.append('svc_merc_slot_cl() 仍按解锁状态拒绝')

    slots = open(MERC_SLOTS, encoding='utf-8').read()
    if 'TouchMercLock' in slots:
        fails.append('merc_slots.py 仍在生成 TouchMercLock 暗红盖板')

    i18n = open(I18N, encoding='utf-8').read()
    if 'dmMercSlotLocked' in i18n:
        fails.append('ui_text.tsv 里仍有 dmMercSlotLocked「秘境 10 层解锁」提示')


def runtime_check(fails):
    clang = shutil.which('clang')
    if not clang:
        fails.append('找不到 clang，无法编译 t_req40_merc_slots.c')
        return
    with tempfile.TemporaryDirectory() as td:
        exe = os.path.join(td, 't_req40_merc_slots')
        cmd = [clang, '-O1', '-Wall', '-Wno-unused-function', '-ffunction-sections',
               '-fdata-sections', '-Wl,-dead_strip', '-I', os.path.join(ROOT, 'dm', 'host'),
               '-o', exe, C_TEST, HLE_OS]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            fails.append('编译 t_req40_merc_slots.c 失败：\n' + r.stderr.strip())
            return
        env = dict(os.environ)
        env.pop('DM_TEST_MERC_SLOTS', None)
        r = subprocess.run([exe], capture_output=True, text=True, env=env)
        sys.stdout.write(r.stdout)
        if r.returncode != 0:
            fails.append('运行 t_req40_merc_slots 失败（见上）')


def main():
    fails = []
    source_checks(fails)
    runtime_check(fails)
    if fails:
        print('test_req40_merc_slots_open FAIL')
        for f in fails:
            print('  ✗ ' + f)
        return 1
    print('test_req40_merc_slots_open OK：门槛已移除，新角色 / 普通难度 / 0 层秘境下六格可用')
    return 0


if __name__ == '__main__':
    sys.exit(main())
