#!/bin/bash
# uireg 的起局脚本（真机 iPhone）：UIREG_RUNNER=<本文件> python3 uireg.py iphone <局名…>。接口同 run_mac.sh（<标签> <上限秒> < 触点脚本）。
# 走 ../touch_e2e.sh：它自己取设备锁、按 pid 停 App、设备读本机配置（local_env.sh）。被测的是手机上已经装好的那个包——装包与本脚本之间放过锁的话，包可能已被别人换掉，
#   要保证是自己的包，就在外层先取设备锁把「装包 + 跑」包住（设备锁可重入）。
# 与 Mac 上的差别：只有 iphone 档；跑满上限秒才收（不提前收）；只拉 boot.txt 与命令包日志——截图每张约 20 秒不拉，所以「只看截图」的项在真机上都是未判；
#   没有 d 局的专用存档（PW_SAVE 非空的局不跑）。
# UIREG_PULL="put_00.png put_01.png"：额外拉回这几张截图（读画面的判据要用；每张约 20 秒，算在持锁时间里）。
# uireg run-start script (real-device iPhone): UIREG_RUNNER=<this file> python3 uireg.py iphone <run names...>. Same interface as run_mac.sh (<label> <limit seconds> < touch point script).
# Goes through ../touch_e2e.sh: it takes the device lock itself, stops the App by pid, and reads the device from the local config (local_env.sh). What is tested is the bundle already installed on the phone -- if the lock was released between installing and this script, the bundle may already have been replaced by someone else,
#   so to make sure it is your own bundle, take the device lock in an outer layer first to wrap 'install + run' (the device lock is reentrant).
# Differences from the Mac: only the iphone profile; the run ends only after the full limit in seconds (no early end); only boot.txt and the command packet log are pulled -- screenshots take about 20 seconds each and are not pulled, so 'screenshot-only' items are all unjudged on the real device;
#   there is no dedicated save for the d runs (runs with a non-empty PW_SAVE are not run).
# UIREG_PULL="put_00.png put_01.png": additionally pull back these screenshots (needed for criteria that read the picture; about 20 seconds each, counted in the lock hold time).
unset LANG LC_CTYPE; export LC_ALL=C   # touch_e2e.sh 里「变量后紧跟中文标点」的行在 UTF-8 区域设置下会被 bash 3.2 当成变量名的一部分 | Lines in touch_e2e.sh where 'a variable is immediately followed by Chinese punctuation' are treated by bash 3.2 as part of the variable name under a UTF-8 locale
TAG=$1; CAP=$2; HERE=$(cd "$(dirname "$0")" && pwd)
O=${UIREG_OUT:-${DM_ROOT:-$HOME/dm-work}/run/uireg}/pw/$TAG; mkdir -p "$O"; cat > "$O/in.txt"
[ "${UIREG_MODE:-iphone}" = iphone ] || { echo "计时：真机只有 iphone 档，没跑" > "$O/timing.txt"; exit 2; }
[ -z "${PW_SAVE:-}" ] || { echo "计时：真机上没有存档 ${PW_SAVE}，没跑" > "$O/timing.txt"; exit 2; }
rm -f "$O/boot.txt" "$O/boot_full.txt" "$O/c2s.jsonl" "$O/e2e_c2s.jsonl"
# 触点脚本的裸坐标按 667×375 的 16:9 画面写 → DM_TOUCH_REF 由 App 换算到真机屏；touch_e2e.sh 一定带按键脚本变量，这里给一条永不触发的占位键序，
#   并显式打开前端自动进选角页（DM_FE_AUTO，不带按键脚本时它缺省就开）；命令包日志名与存档由 touch_e2e.sh 自己定
# The touch point script's bare coordinates are written for a 667×375 16:9 picture → converted by the App to the real-device screen via DM_TOUCH_REF; touch_e2e.sh always passes the key script variable, so a placeholder key sequence that never triggers is given here,
#   and front-end auto-entry into the character select page is turned on explicitly (DM_FE_AUTO, which is on by default when no key script is passed); the command packet log name and the save are decided by touch_e2e.sh itself
J=$(python3 -c "
import json,sys
d={'DM_TOUCH_SCRIPT_START':'menu','DM_TOUCH_REF':'667x375','DM_FE_AUTO':'1'}
for kv in sys.argv[1].split():
    k,v=kv.split('=',1)
    if k not in ('DM_C2S_LOG','DM_SAVE_FRESH'): d[k]=v
print(json.dumps(d))" "${PW_ENV:-}")
t0=$(date +%s)
E2E_KEYS='B@OpenPanel:NoSuchPanelEver' E2E_ENV="$J" E2E_PULL="boot.txt c2s.jsonl ${UIREG_PULL:-}" bash "$HERE/../touch_e2e.sh" "$O/in.txt" "$O" "$CAP" > "$O/e2e.log" 2>&1
[ -f "$O/e2e_c2s.jsonl" ] && cp "$O/e2e_c2s.jsonl" "$O/c2s.jsonl"
# ---- 验包 | package check ----
# 包对不对读出来核，每一项都要读得到、对得上，缺字段、空字段一律判不对（四审 Q4-02）：
#   构建号 = dm/tools/version.sh 给被测提交算出的构建号（出包时用 DM_BUILD_NUMBER 指定过的，用 UIREG_EXPECT_BUILD=<数> 说明）；
#   「源码」= 被测提交本身；「显示版本」= version.sh 给被测提交算出的那一行（不带 *）；「工作区」= clean，或只有本机着色器链的三份元数据（它们的版本记在构建输入 chain= 里）。
# 被测提交缺省 = 本脚本所在树的 HEAD，包是从别的树出的就用 UIREG_EXPECT_COMMIT=<提交> 指明。对不上 = 设备上跑的不是这一批的包（没装 / 被别人换了 / 树里有没提交的代码）
# → 整局作废：日志换成一行红，各项都判不通。
# Whether the package is the right one is read out and checked; every item must be readable and match, and a missing or empty field counts as wrong (fourth review Q4-02):
#   build number = the build dm/tools/version.sh computes for the commit under test (if the package was built with DM_BUILD_NUMBER, state it with UIREG_EXPECT_BUILD=<n>);
#   "源码" (source) = that commit itself; "显示版本" (display version) = the line version.sh computes for that commit (no *); "工作区" (working tree) = clean, or only the three local shader-chain metadata files (their version is recorded as chain= in the build inputs).
# The commit under test defaults to HEAD of the tree this script lives in; if the package was built from another tree, name it with UIREG_EXPECT_COMMIT=<commit>. A mismatch = the device is not running this batch's package
# (not installed / replaced by someone else / uncommitted code in the tree) → the whole run is void: the log is replaced by a single failure line and every item fails.
VS="$(git -C "$HERE" rev-parse --show-toplevel 2>/dev/null)/dm/tools/version.sh"
EXPSHA=$(git -C "$HERE" rev-parse "${UIREG_EXPECT_COMMIT:-HEAD}^{commit}" 2>/dev/null); EXP=; EXPLBL=
if [ -n "$EXPSHA" ] && [ -f "$VS" ]; then eval "$(DM_BUILD_NUMBER= DM_BUILD_FLOOR= bash "$VS" "$EXPSHA" | grep -E '^DM_(BUILD|VERSION_LABEL)=' | sed 's/^DM_/UIX_/')"; EXP=${UIX_BUILD:-}; EXPLBL=${UIX_VERSION_LABEL:-}; fi
EXP=${UIREG_EXPECT_BUILD:-$EXP}
GOT=$(LC_ALL=C grep -a -m1 -o 'boot: 版本 [^ ]* 构建 [0-9]*' "$O/boot.txt" 2>/dev/null | awk '{print $NF}')
GOTSHA=$(LC_ALL=C grep -a -m1 -o 'boot: 源码 [0-9a-f]*' "$O/boot.txt" 2>/dev/null | awk '{print $NF}')
GOTDIRTY=$(LC_ALL=C grep -a -m1 'boot: 源码 ' "$O/boot.txt" 2>/dev/null | sed -n 's/.* 工作区 \([^ ]*\).*/\1/p')
GOTLBL=$(LC_ALL=C grep -a -m1 'boot: 显示版本 ' "$O/boot.txt" 2>/dev/null | sed 's/.*boot: 显示版本 //'); WHY=
[ -z "$EXP" ] || [ -z "$EXPLBL" ] && WHY="算不出被测提交 ${EXPSHA:-读不到} 应有的构建号 / 显示版本（${VS}）"
[ -z "$WHY" ] && [ "$GOT" != "$EXP" ] && WHY="构建号 ${GOT:-读不到}，期望 ${EXP}（提交 $(echo "$EXPSHA" | cut -c1-9)${UIREG_EXPECT_BUILD:+，UIREG_EXPECT_BUILD 指定}）"
[ -z "$WHY" ] && [ "$GOTSHA" != "$EXPSHA" ] && WHY="源码 ${GOTSHA:-读不到}，期望 ${EXPSHA}"
[ -z "$WHY" ] && [ -z "$GOTLBL" ] && WHY="显示版本读不到（启动日志里没有「boot: 显示版本」或它是空的）"
[ -z "$WHY" ] && case $GOTLBL in *\*) WHY="显示版本「${GOTLBL}」带 *：出包的树里有没提交的代码改动";; esac
[ -z "$WHY" ] && [ "$GOTLBL" != "$EXPLBL" ] && WHY="显示版本「${GOTLBL}」，期望「${EXPLBL}」"
# 工作区标记（五审 Q5-04）：clean，或「条数:12 位小写十六进制指纹:逗号分隔的仓库相对路径」（run.sh 生成）。严格解析：条数与列表项数一致、无空项无重复，
# 路径只许三份本机元数据的完整相对路径（同名不同目录、旧格式只有文件名的都拒）；其余格式一律拒
# Working-tree marker (fifth review Q5-04): clean, or "count:12 lowercase hex fingerprint:comma-separated repo-relative paths" (generated by run.sh). Strict parse: the count equals the number of entries, no empty or duplicate entries,
# and only the full relative paths of the three local metadata files are allowed (same name in another directory and the old basename-only format are refused); any other format is refused
DIRTY_RE='^([1-9][0-9]*):([0-9a-f]{12}):([^,]+(,[^,]+)*)$'
if [ -z "$WHY" ]; then case $GOTDIRTY in
  clean) ;;
  '') WHY="工作区标记读不到（启动日志「boot: 源码 … 工作区 …」）";;
  *) if [[ ! $GOTDIRTY =~ $DIRTY_RE ]]; then WHY="工作区标记「${GOTDIRTY}」格式不对（应为 clean 或 条数:12 位指纹:相对路径列表）"
     else DN=${BASH_REMATCH[1]}; DL=${BASH_REMATCH[3]}; DC=0; DSEEN=,
       IFS=, read -r -a DFS <<< "$DL"
       for f in "${DFS[@]}"; do DC=$((DC + 1))
         case $DSEEN in *",$f,"*) WHY="工作区标记「${GOTDIRTY}」里 ${f} 重复"; break;; esac; DSEEN="$DSEEN$f,"
         case $f in dm/gfx/nvn/c1_full.tsv|dm/gfx/nvn/dmsh_chain.h|dm/gfx/tools/dmsh_chain.lock) ;; *) WHY="工作区「${GOTDIRTY}」里有没提交的代码改动 ${f}"; break;; esac
       done
       [ -z "$WHY" ] && [ "$DC" != "$DN" ] && WHY="工作区标记「${GOTDIRTY}」条数 ${DN} 与列表 ${DC} 项对不上"
     fi;;
esac; fi
if [ "${UIREG_BUILD_CHECK:-1}" = 1 ] && [ -f "$O/boot.txt" ] && [ -n "$WHY" ]; then
  mv "$O/boot.txt" "$O/boot_wrongpkg.txt"; echo "[dm] 【红】包不对，整局作废：设备上这一局的${WHY}；原日志在 boot_wrongpkg.txt" > "$O/boot.txt"
fi
# ---- 验包完 | end of package check ----
# 真机跑满上限秒才收：脚本走完之后静置的那一段（会有「无进展」看门狗红行）不属于这一局，日志截到「note 结束」为止，与 Mac 上见到记号就收的范围一致；全文留在 boot_full.txt
# On the real device the run only ends after the full limit in seconds: the idle stretch after the script finishes (which gets 「无进展」(no progress) watchdog failure lines) does not belong to this run, so the log is cut at 「note 结束」(note end), matching the range on the Mac where the run ends as soon as the marker is seen; the full text is kept in boot_full.txt
if LC_ALL=C grep -a -q '\[script\] note 结束' "$O/boot.txt" 2>/dev/null; then mv "$O/boot.txt" "$O/boot_full.txt"; LC_ALL=C awk '{ print } /\[script\] note 结束/ { exit }' "$O/boot_full.txt" > "$O/boot.txt"; fi
echo "计时：真机整局墙钟 $(( $(date +%s) - t0 )) 秒（含排锁、推脚本、拉日志）" > "$O/timing.txt"
