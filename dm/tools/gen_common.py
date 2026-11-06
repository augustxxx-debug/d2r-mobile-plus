# 手机端生成与电脑上的 iOS 壳工具共用的执行与跳步框架（d2rm_android.py、d2rm_pack.py）。它进安卓包的 src.zip，壳工具本身不进（dm/android/tools/pack_gen_assets.py）。
# The run-and-skip framework shared by on-phone generation and the computer's iOS shell tool (d2rm_android.py, d2rm_pack.py). It ships in the Android package's src.zip; the shell tool does not.
import os, runpy, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.environ.get('DM_ROOT') or os.path.expanduser('~/dm-work'))
PY = sys.executable
WINLF = os.path.join(HERE, 'winlf'); runpy.run_path(os.path.join(WINLF, 'sitecustomize.py'))   # Windows 上统一用 \n 换行（见该目录的说明）：本进程在这里加载，子脚本经 PYTHONPATH 自动加载 | Use LF line endings on Windows (see the note in that directory): loaded here for this process, and automatically through PYTHONPATH for child scripts
MODS = ['main', 'sdk']


def say(s): print('[%s] %s' % (time.strftime('%H:%M:%S'), s), flush=True)


def die(s): sys.exit('[x] ' + s)   # 不用 ✗：中文 Windows 的控制台（GBK）打不出来，会在报错的地方再抛一个编码异常 | no ✗: the console of Chinese Windows (GBK) cannot print it, and would raise another encoding exception at the point of the error report


def run(cmd, env=None, cwd=None, log=None, what=None):
    e = dict(os.environ, DM_ROOT=ROOT, PYTHONUTF8='1', PYTHONPATH=os.pathsep.join(filter(None, [WINLF, os.environ.get('PYTHONPATH')]))); e.update(env or {})   # 子脚本一律按 UTF-8 读写与打印（Windows 缺省是本地编码，读我们的源文件、打 ✓ 都会出错） | child scripts always read, write and print in UTF-8 (the Windows default is the local encoding, and both reading our source files and printing ✓ would fail)
    r = subprocess.run(cmd, env=e, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if log: open(log, 'wb').write(r.stdout)
    # 失败时先写最里层的报错、再附这一步输出的最后 120 行（genfail.fail_report）：以前只取最后一行，套着包装层（winlf/run.py、子进程 check=True）时只剩外层的
    # CalledProcessError，看不出真正的原因（缺陷台账 B026）
    # On failure write the innermost error first, then the last 120 lines of this step's output (genfail.fail_report): previously only the last line was kept, which
    # behind a wrapper (winlf/run.py, a child process with check=True) left only the outer CalledProcessError and hid the real cause (defect ledger B026)
    if r.returncode:
        import genfail
        die(genfail.fail_report(what or cmd[0], r.returncode, r.stdout.decode('utf-8', 'replace'), log))
    return r.stdout.decode('utf-8', 'replace')


def step(name, done, fn):
    if done(): say('%s：已有，跳过' % name); return
    t0 = time.time(); say(name); fn()
    if not done(): die('%s 做完了但没看到产物' % name)
    say('%s 完成（%d 秒）' % (name, time.time() - t0))
