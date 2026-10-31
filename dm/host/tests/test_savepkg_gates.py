#!/usr/bin/env python3
# 存档管理的构建和外壳闸门（外审 S-08、S-09、S-10），t_savepkg 够不到的部分。核心的反例都在 t_savepkg.c。
# Build and shell gates of save management (external audit S-08, S-09, S-10), the parts t_savepkg cannot reach. The core's counter-examples live in t_savepkg.c.
import os, re, shutil, subprocess, sys, tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..')); HOST = ROOT + '/host'
def fail(m): raise SystemExit('FAIL test_savepkg_gates: ' + m)
def form(*a):
    t = subprocess.run(['make', '-pn', '-C', HOST, 'dmhost', 'DM_ROOT=/nonexistent', *a], capture_output=True, text=True).stdout
    cf = [l for l in t.split('\n') if l.startswith('CFLAGS') and ('=' in l)]; dep = re.search(r'^dmhost: (.*)$', t, re.M)
    return ' '.join(cf), dep.group(1) if dep else ''
for args, on in (((), False), (('DM_QA_BUILD=0',), False), (('DM_QA_BUILD=',), False), (('DM_QA_BUILD=1',), True)):
    cf, dep = form(*args)
    if ('-DDM_QA_BUILD=1' in cf) != on: fail(f'{args} 的 QA 宏不对')
    if ('.qa_build_on' if on else '.qa_build_off') not in dep: fail(f'{args} 切形态不会重编：{dep[:80]}')
# 连着切形态也要重编（内审 R2-02）：用 Makefile 里真实的戳文件规则，配一个假产物 | switching forms back to back rebuilds: the real stamp rule with a fake output
mk = open(HOST + '/Makefile', encoding='utf-8').read(); rule = re.search(r'^\$\(QA_STAMP\):\n\t.*$', mk, re.M); form_block = re.search(r'^ifeq \(\$\(DM_QA_BUILD\),1\)\n.*?^endif\n', mk, re.M | re.S)
if not rule or not form_block: fail('Makefile 里找不到戳文件规则')
with tempfile.TemporaryDirectory() as d:
    open(d + '/Makefile', 'w').write(form_block.group(0) + rule.group(0) + '\ndmhost: $(QA_STAMP)\n\techo $(QA_STAMP) > $@\n')
    for args, want in ((['DM_QA_BUILD=1'], 'on'), ([], 'off'), (['DM_QA_BUILD=1'], 'on'), ([], 'off'), ([], 'off')):
        subprocess.check_output(['make', '-C', d, 'dmhost', *args], stderr=subprocess.STDOUT)
        if want not in open(d + '/dmhost').read(): fail(f'连着切形态没有重编：{args}')
g = open(ROOT + '/android/app/build.gradle', encoding='utf-8').read()
if "findProperty('dmQaBuild') == '1'" not in g: fail('build.gradle 的 QA 开关只认 1')
if not re.search(r'strings .*\$L.*DM_TEST_\(UI\|', open(ROOT + '/android/tools/shell_apk.sh', encoding='utf-8').read()): fail('shell_apk.sh 出包不查 QA 入口')
if not re.search(r'strings .*dmhost.*DM_TEST_\(UI\|', open(ROOT + '/tools/mac_app.sh', encoding='utf-8').read()): fail('mac_app.sh 出包不查 QA 入口')
# 驱动动作之后才读状态（S-10：它可能同一帧进 S_WORK 又回报完成）| state is read after the driver
s = open(HOST + '/dm_savemgr.c', encoding='utf-8').read(); i = s.index('void dm_savemgr_pump(void)'); p = s[i:]
if not p.index('qa_pump(root);') < p.index('int st = atomic_load(&g_st);'): fail('qa_pump 要在读状态之前')
if 'if (atomic_load(&g_st) == S_WORK) show_msg(S(sd > 0' not in p: fail('分享完成要按当前状态判')
# 同包重名有自己的提示，不落回「校验不符」（外审 R2-01）| a same-name package has its own message, not the generic check failure
if 'case DM_PKG_E_DUP: return "dmSaveErrDup"' not in open(HOST + '/dm_savemgr.c', encoding='utf-8').read(): fail('DM_PKG_E_DUP 要对应 dmSaveErrDup')
# 安卓选文件的上限（S-09）| the Android pick cap
J = ROOT + '/android/app/src/main/java/com/xiaoboluo/d2r'; a = open(J + '/DmActivity.java', encoding='utf-8').read()
if 'SaveCopy.copy(in, out, SaveCopy.PICK_MAX)' not in a or 'tmp.delete()' not in a: fail('安卓选文件没走上限')
jh = os.environ.get('JAVA_HOME') or os.path.expanduser('~/dm-android/jdk'); jc = shutil.which('javac', path=':'.join(d for d, _, fs in os.walk(jh) if 'javac' in fs)) if os.path.isdir(jh) else None
if not jc: print('test_savepkg_gates: 没有 JDK，SaveCopyTest 跳过')
else:
    with tempfile.TemporaryDirectory() as d:
        subprocess.check_call([jc, '-d', d, J + '/SaveCopy.java', ROOT + '/android/tests/SaveCopyTest.java'])
        subprocess.check_call([os.path.join(os.path.dirname(jc), 'java'), '-cp', d, 'com.xiaoboluo.d2r.SaveCopyTest'])
print('PASS test_savepkg_gates')
