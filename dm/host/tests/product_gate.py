#!/usr/bin/env python3
# 出包入口的负控（外审 X-05）：副本树里改掉 patch_nso.py，只重出产物（build_src.sha256 跟上）、不动 dm/PRODUCT，run.sh build 和 mac_app.sh 都必须在打包前失败，DM_OUT 也绕不过去；正控是没改的副本能走过这道。
# Negative control for the packaging entries (external review X-05): in a copied tree, change patch_nso.py, refresh the output only (build_src.sha256 follows) and leave dm/PRODUCT alone; run.sh build and mac_app.sh must fail before packaging, DM_OUT included; the positive control is an untouched copy passing this step.
import hashlib, os, shutil, subprocess, sys, tempfile

DM = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
FILES = ('PRODUCT', 'tools/product.sh', 'tools/patch_nso.py', 'tools/mac_app.sh', 'ios/run.sh', 'ios/tools/devlock.sh', 'ios/tools/asc_env.sh', 'ios/tools/local_env.sh')


def tree(root, tamper):
    for f in FILES:
        os.makedirs(os.path.dirname(os.path.join(root, 'dm', f)), exist_ok=True); shutil.copy(os.path.join(DM, f), os.path.join(root, 'dm', f))
    nso = os.path.join(root, 'dm/tools/patch_nso.py')
    if tamper: open(nso, 'a').write('# changed\n')
    out = os.path.join(root, 'out', open(os.path.join(root, 'dm/PRODUCT')).readline().strip() + '-touch'); os.makedirs(out)
    open(os.path.join(out, 'build_src.sha256'), 'w').write(hashlib.sha256(open(nso, 'rb').read()).hexdigest() + '  patch_nso.py\n')
    open(os.path.join(out, 'main.patched.bin'), 'w').close()
    os.makedirs(os.path.join(root, 'build/cache/dm/out')); os.rename(out, os.path.join(root, 'build/cache/dm/out', os.path.basename(out)))
    return os.path.join(root, 'build/cache/dm/out', os.path.basename(out))


def run(root, cmd, **env):
    e = dict(os.environ, DM_ROOT=root, DM_RUN_DIR=os.path.join(root, 'run'), DM_BUNDLE_ID='test.gate', DM_LOCAL_ENV=os.devnull, PATH='/usr/bin:/bin:/usr/sbin:/sbin', **env)
    e.pop('DM_OUT', None) if 'DM_OUT' not in env else None
    return subprocess.run(['bash'] + cmd, cwd=root, env=e, capture_output=True, text=True, timeout=60)


fail = 0
def check(name, ok, detail=''):
    global fail
    print('PRODUCT_GATE %s %s%s' % ('PASS' if ok else 'RED', name, '' if ok else ' ' + detail[-400:])); fail += not ok


with tempfile.TemporaryDirectory() as root:
    out = tree(root, True)
    r = run(root, ['dm/tools/product.sh', 'fresh', out]); check('tampered tree: output looks fresh (the hole)', r.returncode == 0, r.stderr)
    r = run(root, ['dm/tools/product.sh', 'lock']); check('tampered tree: lock refuses', r.returncode == 1 and '代号' in r.stderr, r.stderr)
    r = run(root, ['dm/ios/run.sh', 'build']); check('tampered tree: run.sh refuses', r.returncode == 1 and '代号' in r.stdout + r.stderr, r.stdout + r.stderr)
    r = run(root, ['dm/ios/run.sh', 'build'], DM_OUT=out); check('tampered tree: run.sh refuses with DM_OUT', r.returncode == 1 and '代号' in r.stdout + r.stderr, r.stdout + r.stderr)
    r = run(root, ['dm/tools/mac_app.sh', os.path.join(root, 'mac')]); check('tampered tree: mac_app.sh refuses', r.returncode == 1 and '代号' in r.stdout + r.stderr, r.stdout + r.stderr)
    r = run(root, ['dm/tools/product.sh', 'rev']); check('second line stays out of the codename', r.returncode == 0 and r.stdout.strip().startswith('r') and '\n' not in r.stdout.strip(), r.stdout)
with tempfile.TemporaryDirectory() as root:
    tree(root, False)
    r = run(root, ['dm/tools/product.sh', 'lock']); check('untouched tree: lock passes', r.returncode == 0, r.stderr)
sys.exit(1 if fail else 0)
