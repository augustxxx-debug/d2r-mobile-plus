#!/usr/bin/env python3
# 改写器与代号绑在一起（内审 M-03）：dm/PRODUCT 第二行登记 patch_nso.py 的指纹，对不上就红。
# 这条拦的是「改了改写器却忘了升代号」：产物目录按代号共享，安卓手机上已生成的内容也只认代号，不升就不会重新生成，玩家继续用旧程序。
# 改了会改变产物的内容就升第一行的代号；只改注释可以不升。两种情况都要更新第二行（python3 product_lock.py --update）。
# 这里是单测；出包入口 run.sh、mac_app.sh 走 product.sh lock 同一条判定（外审 X-05），负控在 product_gate.py。
# Binds the rewriter to the codename (internal audit M-03): the second line of dm/PRODUCT records the fingerprint of patch_nso.py; a mismatch is red.
# This catches "changed the rewriter, forgot the codename": output directories are shared by codename, and content already generated on an Android phone goes by the codename alone, so without a bump it is not regenerated and players keep the old program.
# A change that alters the output bumps the codename on the first line; a comment-only change need not. Either way update the second line (python3 product_lock.py --update).
# This is the unit test; the packaging entries run.sh and mac_app.sh apply the same check through product.sh lock (external review X-05), with negative controls in product_gate.py.
import hashlib, os, sys

DM = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
product = os.path.join(DM, 'PRODUCT')
lines = open(product).read().splitlines()
now = hashlib.sha256(open(os.path.join(DM, 'tools', 'patch_nso.py'), 'rb').read()).hexdigest()
if sys.argv[1:] == ['--update']:
    open(product, 'w').write('%s\npatch_nso %s\n' % (lines[0], now)); print('PRODUCT_LOCK 已更新：%s' % lines[0]); sys.exit(0)
have = lines[1].split()[1] if len(lines) > 1 and lines[1].startswith('patch_nso ') else None
if have != now:
    sys.exit('PRODUCT_LOCK RED patch_nso.py 改过了（登记 %s，当前 %s）：会改变产物内容就升 dm/PRODUCT 第一行的代号，然后跑 product_lock.py --update 和 product.sh build' % (have, now))
print('PRODUCT_LOCK PASS %s' % lines[0])
