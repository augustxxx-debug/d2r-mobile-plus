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
# 选文件夹的副本：数量与总量都封顶，超了就整份不留（2026-11-01 审计）| a picked folder's copy: capped by count and total, and nothing is left behind past them (the 2026-11-01 audit)
if 'new SaveCopy.Budget(SaveCopy.PICK_MAX)' not in a or 'budget.fits(' not in a: fail('安卓选文件夹的副本没有数量 / 总量上限')
# 两个选进来的副本都有自己的目录，启动时一起清（临时树清理）| both picked copies live in their own directory and are cleared at the next start (temporary-tree cleanup)
if '"temp/save_pick"' not in a: fail('安卓选文件的副本没有自己的目录，启动时清不掉')
if 'saveDeleteTree(new File(new File(getFilesDir(), "temp"), "save_pick"))' not in a: fail('安卓启动时不清选文件的副本')
# iOS 同一套上限：数量 4096、总量 320 MiB（与安卓、与 zf_open 相同）| iOS carries the same caps: 4096 files, 320 MiB (as Android and zf_open)
i = open(ROOT + '/ios/Sources/dm_savemgr_ios.m', encoding='utf-8').read()
for want in ('4096', '320ll << 20'):
    if want not in i: fail('iOS 选文件夹的上限缺 ' + want)
# 外壳把「!limit / !io」交给宿主：选文件夹超限走「存档包不符合要求」，与单个文件一致 | the shells hand "!limit / !io" to the host: a folder past the caps gets the package-invalid message, as a single file does
if "p[1] == 'l' ? DM_PKG_E_LIMIT : " not in s: fail('外壳的 !limit 没接到 DM_PKG_E_LIMIT')
# 审计 20261104 P1-1：闸本身在 t_savepkg 的 t_job_gate 里测；这里查宿主、外壳真的接上了 | the gate itself is tested by t_job_gate; this checks the host and shells are wired to it
js = s[s.index('static int job_start('):]; js = js[:js.index('\n}')]
if js.index('dm_savejob_begin(&g_jb)') > js.index('pthread_create'): fail('job_start 要先占位再起线程')
rp = s[s.index('static void request_pick('):]; rp = rp[:rp.index('\n}')]
if not (rp.index('go(S_WORK)') < rp.index('pick();') and rp.index('dm_savejob_pick_arm(&g_jb)') < rp.index('pick();')): fail('选文件前要先进处理中框并挂号（拷贝期间界面锁住）')
if 'dm_savejob_busy(&g_jb)' not in rp: fail('后台活没完时不能开选文件')
pk = p[p.index('atomic_exchange(&g_picked_new, 0)'):]; pk = pk[:pk.index('st = atomic_load(&g_st); }')]
if pk.index('dm_savejob_pick_accept(&g_jb)') > pk.index('start_import('): fail('选文件回调要先销号再起活')
if 'dm_savejob_take(&g_jb, &rc)' not in p or 'st == S_WORK && atomic_exchange' in p: fail('后台活结果要经 dm_savejob_take 取（取了才放开占位）')
if 'g_pick_wait' in s or 'g_job_done' in s: fail('旧的只写不读的标志还在')
# 进度：换段前先清零、读端先读段再读进度（P1-2）| progress: zero before switching steps, the reader reads the step before the count
jm = s[s.index('static void *job_main('):]; jm = jm[:jm.index('\n}')]
if not jm.index('dm_savepkg_progress_reset()') < jm.index('atomic_store(&g_bar_half, 0)'): fail('换段前要先清零进度')
br = p[p.index('TouchSaveBarBorder'):]
if not br.index('atomic_load(&g_bar_half)') < br.index('dm_savepkg_progress_read('): fail('进度条要先读段再读进度')
# P1-3：iOS 拷之前判联机、把原路径交给宿主；!coop 接到 E_COOP | iOS judges co-op before copying and passes the original path; !coop maps to E_COOP
if "p[1] == 'c' ? DM_PKG_E_COOP" not in s: fail('外壳的 !coop 没接到 DM_PKG_E_COOP')
if 'dm_savepkg_wrap_foreign_from(g_job_arg, g_job_from' not in s: fail('校验没带原路径')
pi = i[i.index('didPickDocumentsAtURLs'):i.index('documentPickerWasCancelled')]
if pi.index('dm_savepkg_is_coop_root(') > pi.index('copyItemAtURL'): fail('iOS 要在拷贝之前判联机')
if 'dm_savemgr_picked_from(out.fileSystemRepresentation, ' not in pi: fail('iOS 没把原路径交给宿主')
# P1-1 c：两端外壳串行拷、每次一个新目录 | both shells copy one at a time into a new directory each pick
if 'dispatch_async(pick_queue()' not in pi or 'DISPATCH_QUEUE_SERIAL' not in i or 'NSUUID.UUID.UUIDString' not in pi: fail('iOS 选文件的拷贝要串行、每次一个新目录')
if 'dispatch_get_global_queue' in pi: fail('iOS 选文件不能再走并发队列')
ap = a[a.index('protected void onActivityResult('):]; ap = ap[:ap.index('} else {')]
if ap.count('SaveCopy.PICKS.execute(') != 2 or ap.count('SaveCopy.freshDir(') != 2 or 'new Thread(' in ap: fail('安卓两种选择都要串行、每次一个新目录')
if ap.count('nativeSavePickedFrom(') != 2: fail('安卓没把文档 ID 交给宿主')
if 'Java_com_xiaoboluo_d2r_DmActivity_nativeSavePickedFrom' not in open(ROOT + '/android/text_input_jni.c', encoding='utf-8').read(): fail('nativeSavePickedFrom 没有 JNI 实现')
jh = os.environ.get('JAVA_HOME') or os.path.expanduser('~/dm-android/jdk'); jc = shutil.which('javac', path=':'.join(d for d, _, fs in os.walk(jh) if 'javac' in fs)) if os.path.isdir(jh) else None
if not jc: print('test_savepkg_gates: 没有 JDK，SaveCopyTest 跳过')
else:
    with tempfile.TemporaryDirectory() as d:
        subprocess.check_call([jc, '-d', d, J + '/SaveCopy.java', ROOT + '/android/tests/SaveCopyTest.java'])
        subprocess.check_call([os.path.join(os.path.dirname(jc), 'java'), '-cp', d, 'com.xiaoboluo.d2r.SaveCopyTest'])
print('PASS test_savepkg_gates')
