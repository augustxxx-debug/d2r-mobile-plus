#!/bin/bash
# 当前改写产物的代号只写在一处：dm/PRODUCT 第一行（如 3.3.94012-r28）。换代只改那一行。第二行登记代号对应的 patch_nso.py 指纹：改了改写器就要看一眼代号（host/tests/product_lock.py）。
# 用法：product.sh name <种类>  → 3.3.94012-r28-<种类>（种类：touch / rules / keypage）
#       product.sh path <种类>  → 本机缺省产物目录 $DM_ROOT/build/cache/dm/out/<name>
#       product.sh rev           → r28
#       product.sh build         → 重出三种产物（keypage / rules / touch）到本机缺省目录
#       product.sh lock          → 第二行登记的指纹与当前 patch_nso.py 比，出包入口都先过这条
# The codename of the current rewrite build output is written in exactly one place: the first line of dm/PRODUCT (e.g. 3.3.94012-r28). A revision bump changes only that line. The second line records the patch_nso.py fingerprint for this codename: changing the rewriter means looking at the codename (host/tests/product_lock.py).
# Usage: product.sh name <kind>  → 3.3.94012-r28-<kind> (kind: touch / rules / keypage)
#       product.sh path <kind>  → local default build output directory $DM_ROOT/build/cache/dm/out/<name>
#       product.sh rev           → r28
#       product.sh build         → rebuild the three kinds of build output (keypage / rules / touch) into the local default directory
#       product.sh lock          → compare the fingerprint on the second line with the current patch_nso.py; every packaging entry passes this first
set -u
P=$(head -1 "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/PRODUCT") || exit 2
[ -n "$P" ] || { echo "dm/PRODUCT 为空" >&2; exit 2; }
case "${1:-}" in
  name) echo "$P-${2:?种类}";;
  path) echo "${DM_ROOT:-$HOME/dm-work}/build/cache/dm/out/$P-${2:?种类}";;
  rev)  echo "${P##*-}";;
  build)   # 用本工作树的改写器重出三种产物到本机缺省目录（换代 / 改了 patch_nso.py 之后跑）。种类 → 规则集：keypage = 原生；rules = 触屏规则、不带宿主私有门；touch = 触屏规则 + 私有门 | Rebuild the three kinds of build output with this worktree's rewriter into the local default directory (run after a revision bump / after changing patch_nso.py). Kind → rule set: keypage = native; rules = touchscreen rules, without host-private gates; touch = touchscreen rules + private gates
    HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd); PY=${DM_PY:-${DM_ROOT:-$HOME/dm-work}/build/cache/dm/venv/bin/python3}; NSO=${DM_NSO_DIR:-${DM_ROOT:-$HOME/dm-work}/data/switch/3.3.94012/Update/ExeFS}
    for k in keypage rules touch; do O=${DM_ROOT:-$HOME/dm-work}/build/cache/dm/out/$P-$k; mkdir -p "$O"
      case $k in keypage) E=(DM_RULESET=native);; rules) E=(DM_RULESET=touch-run-v4 DM_PRIVATE_GATES=0);; touch) E=(DM_RULESET=touch-run-v4);; esac
      for m in main sdk subsdk0; do env "${E[@]}" "$PY" "$HERE/patch_nso.py" "$NSO/$m" "$O" "$m" > "$O/$m.patch.log" 2>&1 || { echo "🔴 $k $m 改写失败：$(tail -2 "$O/$m.patch.log")" >&2; exit 1; }; done
      echo "$P-$k 已出：$(ls "$O" | wc -l | tr -d ' ') 个文件"; done;;
  lock)    # 改了改写器却没升代号：产物目录和手机上的生成结果都只认代号，重出产物也不算数，DM_OUT 也不例外（外审 X-05） | changed the rewriter without bumping the codename: output directories and what phones generated go by the codename alone, so rebuilding does not count, nor does DM_OUT (external review X-05)
    HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd); want=$(shasum -a 256 "$HERE/patch_nso.py" | cut -d' ' -f1); have=$(sed -n 's/^patch_nso //p' "$HERE/../PRODUCT")
    [ "$have" = "$want" ] || { echo "🔴 patch_nso.py 改过了（登记 ${have:-没有}，当前 ${want}）：会改变产物内容就升 dm/PRODUCT 第一行的代号，然后跑 python3 dm/host/tests/product_lock.py --update 和 product.sh build" >&2; exit 1; };;
  fresh)   # 产物是不是当前改写器出的：build_src.sha256 与当前 patch_nso.py 比（内审 M-03）。fresh <产物目录> | whether a build output came from the current rewriter: compare build_src.sha256 with the current patch_nso.py (internal audit M-03). fresh <output directory>
    HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd); O=${2:?产物目录}; want=$(shasum -a 256 "$HERE/patch_nso.py" | cut -d' ' -f1); have=$(cut -d' ' -f1 "$O/build_src.sha256" 2>/dev/null)
    [ "$have" = "$want" ] || { echo "🔴 产物 $O 不是当前 patch_nso.py 出的（产物记录 ${have:-没有}，当前 ${want}）：重出产物，跑 product.sh build" >&2; exit 1; };;
  *) echo "用法：product.sh name|path <种类> | rev | build | lock | fresh <产物目录>" >&2; exit 2;;
esac
