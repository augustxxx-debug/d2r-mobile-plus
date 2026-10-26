#!/bin/bash
# 功耗基准（真机、无人值守）：一次启动跑完整个渲染分辨率档位矩阵，游戏内按脚本走动、轮换关卡、定时刷怪；每 5 秒记一行电量 / 温控 / 帧率 / GPU，
# 结束后拉回 Documents/bench_<run>/ 并生成报告。测的是整机电量下降速率（%/小时）。系统电量读数是量化的（iPhone Duo 上 5% 一档），短段分不出高低；报告按实际步长给误差、不可比时不排名（bench_report.py）。
# 用法：dm/ios/tools/power_bench.sh <每段分钟> <档位序列如 1,3,0,2> [MetalFX: keep|off|auto|eco|balanced|quality（缺省 keep）] [存档备份目录（首次建 save.test 用）]
#   例：dm/ios/tools/power_bench.sh 12 1,3,0,2 keep "$DM_ROOT/run/save-backup/<时刻>/<子目录>"
# 流程：① 取设备锁（整个测量期间持有：手机在跑基准，别的会话不该装机或启动）；② 锁内核对设备在、目标 App 没在跑（按安装目录识别，见 bench_device.py）、
#   Documents/save.test 是否存在（读不到设备直接退出，不当作“不存在”）；不在就从备份目录推一份（绝不碰 Documents/save）；③ 带环境变量启动，**不带 --terminate-existing**，
#   不会结束任何已在运行的进程；④ 提示拔线，等待 总时长+1 分钟；⑤ 等设备接回，只结束目标 App 的进程、拉回数据、出报告，最后放锁。
# 不装机：装机用 run.sh install。产物：$DM_RUN_DIR 或 $DM_ROOT/run/ios-fable5/bench/<run>/
# Power benchmark (real device, unattended): one launch runs the whole render-resolution matrix; in-game the script walks, cycles through levels and spawns monsters on a timer;
# one row every 5 s with battery / thermal / fps / GPU, then results are pulled from Documents/bench_<run>/ and a report is written. Measures whole-device battery drain (%/hour);
# the battery reading is quantized (5% steps on the iPhone Duo), so short segments cannot separate levels; the report uses the actual step and refuses to rank when not comparable (bench_report.py).
# Usage: dm/ios/tools/power_bench.sh <minutes per segment> <level sequence e.g. 1,3,0,2> [MetalFX: keep|off|auto|eco|balanced|quality (default keep)] [save backup dir (to create save.test the first time)]
# 整个脚本体放在函数里再调用：bash 边读边执行，跑着的时候再编辑文件会把新内容当命令执行（2026-10-26 基线收尾因此失败）
# The whole body lives in a function that is called at the end: bash reads scripts incrementally, and editing the file while it runs executes the new text as commands (that is how the 2026-10-26 baseline lost its wrap-up)
power_bench_main() {
set -u
set -o pipefail   # 管道里任何一段失败都算失败（报告生成失败不能被 tee 掩盖，复审 F07） | any failing stage fails the pipeline (a failed report must not be masked by tee, re-audit F07)
cd "$(dirname "$0")/.."
source tools/local_env.sh && local_env_need DM_DEVICE DM_BUNDLE_ID || exit 1
export DM_DEVBATTERY=0 DM_DEVLOCK_CLASS=measure
source tools/devlock.sh
SEG=${1:?每段分钟}; LEVELS=${2:?档位序列，如 1,3,0,2}; FX=${3:-keep}; SAVESRC=${4:-}
D=$DM_DEVICE; B=$DM_BUNDLE_ID; RUN=$(date +%Y%m%d-%H%M%S)
OUT=${DM_RUN_DIR:-${DM_ROOT:-$HOME/dm-work}/run/ios-fable5}/bench/$RUN; mkdir -p "$OUT"
t(){ echo "[$(date +%T)] $*" | tee -a "$OUT/bench.log"; }
present(){ xcrun devicectl list devices 2>/dev/null | grep -q "$D"; }
BD=(python3 -I tools/bench_device.py)
SEG_CHECK=$((SEG * 60)); NLV=$(echo "$LEVELS" | tr ',' '\n' | wc -l | tr -d ' ')
# 设备锁：从第一次碰设备到拉回数据一直持有；上限 = 总时长 + 最长等接回 12 小时 + 30 分钟余量（看门狗超限会结束本脚本） | Device lock: held from the first device access until the data is pulled; limit = run time + up to 12 h waiting for reconnection + 30 min margin (the watchdog ends this script past the limit)
devlock_acquire "power_bench $RUN" $((NLV * SEG_CHECK + 43200 + 1800)) || exit 1
trap devlock_release EXIT
present || { t "🔴 设备没接上（devicectl 看不到 ${D}）"; exit 1; }
STATE=$("${BD[@]}" state "$D" "$B") || { t "🔴 读不到设备状态（见上一行），不继续"; exit 1; }
case "$STATE" in *app=absent*) t "🔴 手机上没装目标 App"; exit 1;; *app=running*) t "🔴 目标 App 正在运行（${STATE%%$'\n'*}）：请先在游戏里保存退出并关闭 App，再跑基准"; exit 1;; esac
# ---- 测试存档：只用 Documents/save.test，缺了就从备份推一份 | Test save: only Documents/save.test; push one from the backup if missing
if [ "${STATE##*savetest=}" != yes ]; then
  [ -n "$SAVESRC" ] && [ -d "$SAVESRC/save" ] || { t "🔴 设备上没有 Documents/save.test，且没给存档备份目录（第 4 个参数，里面要有 save/）"; exit 1; }
  xcrun devicectl device copy to --device "$D" --domain-type appDataContainer --domain-identifier "$B" --source "$SAVESRC/save" --destination Documents/save.test > "$OUT/push_save.log" 2>&1 || { t "🔴 推测试存档失败（见 $OUT/push_save.log）"; exit 1; }
  t "已从备份建立 Documents/save.test（Documents/save 未动）"
fi
# ---- 时间表 | Schedule
IFS=, read -r -a LV <<< "$LEVELS"; N=${#LV[@]}; SEGS=$((SEG * 60)); TOTAL=$((N * SEGS))
SCHED=""; for i in "${!LV[@]}"; do SCHED="$SCHED${SCHED:+,}${LV[$i]}@$((i * SEGS))"; done
# 进局：主菜单 X 开选角 → 载入第 0 个角色 → 进游戏；之后左摇杆 左→上→右→下 各 10 秒循环（最多 256 项，首 2 项进局）
# Enter the game: X on the main menu opens character select → load character 0 → into the game; then left stick left→up→right→down 10 s each in a loop (at most 256 entries, first 2 enter the game)
SLOT=${BENCH_SLOT:-0}   # 选角列表里第几个角色（0 起）；日志「角色「…」进局」行核对实际进的是谁 | Which character in the select list (0-based); verify with the log line naming the character that entered
K="X@OpenPanel:MainMenuButtonRibbon>OpenPanel:CharacterSelectPanel,MSG:CharacterSelect:LoadCharacter:$SLOT@OpenPanel:CharacterSelectPanel%1>FrontEndNavigation:ToGame"
WALK=10; NW=$(( TOTAL / WALK + 6 )); [ $NW -gt 253 ] && { WALK=$(( TOTAL / 250 + 1 )); NW=$(( TOTAL / WALK + 3 )); }
K="$K,LSL@UnloadPanel:LoadScreenPanel+$WALK"; for i in $(seq 1 $((NW - 1))); do K="$K,$(echo LSU LSR LSD LSL | cut -d' ' -f$(( (i - 1) % 4 + 1 )))@+$WALK"; done
# 关卡轮换每 90 秒（2250 拍，25 拍/秒）：血沼 → 邪恶洞穴 → 寒冷平原 → 埋骨之地；刷怪：每个周期第 20 秒刷 4 只沉沦魔、第 50 秒再刷 8 只（类号 19；若不对看日志里的「刷类号」行）
# Level rotation every 90 s (2250 ticks, 25 ticks/s): Blood Moor → Den of Evil → Cold Plains → Burial Grounds; spawns: 4 fallen at second 20 of each period, 8 more at second 50 (class 19; check the spawn log line if wrong)
WARP='every2250:2,8,3,17'
# 刷怪：每个周期（第一次传送之后，城里不刷）第 20 秒刷 4 只沉沦魔（19）+ 第 30 秒刷 6 只恶魔萨满（60，火球、复活）+ 第 40 秒刷 1 只迪亚波罗（243）+ 第 50 秒再刷 8 只沉沦魔；BENCH_SPAWN 可整体覆盖
# Spawns: each period (after the first warp, nothing in town) 4 fallen (19) at second 20 + 6 devilkin shamans (60, fire bolts, resurrect) at second 30 + 8 more fallen at second 50; BENCH_SPAWN overrides
SPAWN=${BENCH_SPAWN:-'every2250+2750:19:4,4*4;every2250+3000:60:-4,-4*6;every2250+3250:243:0,8*1;every2250+3500:19:-5,3*8'}   # 243 = 迪亚波罗：每周期一只，火焰新星 / 红色闪电 / 骨牢的满屏特效 | 243 = Diablo: one per period, fire nova / red lightning / bone prison full-screen effects
# 终局特效：角色拿 20 级暴风雪(59)/陨石(56)/冰封球(64)/连锁闪电(53)/九头蛇(62) 非本职技能，生命法力上限 10 万（×256 定点），每 25 拍（1 秒）轮放一个，半径 6 格；BENCH_CAST=0 关
# Endgame effects: the character gets level-20 non-class Blizzard(59)/Meteor(56)/Frozen Orb(64)/Chain Lightning(53)/Hydra(62), life and mana max 100000 (x256 fixed point), casts one every 25 ticks (1 s) within 6 cells; BENCH_CAST=0 disables
STAT='7=25600000,9=25600000,97:59=20,97:56=20,97:64=20,97:53=20,97:62=20'; CAST=${BENCH_CAST:-every25:59/56/64/53/62:6}
CASTENV=(); [ "$CAST" = 0 ] || CASTENV=(DEVICECTL_CHILD_DM_TEST_STAT="$STAT" DEVICECTL_CHILD_DM_TEST_CAST="$CAST")
[ -z "${BENCH_STATLOG:-}" ] || CASTENV+=(DEVICECTL_CHILD_DM_TEST_STAT_LOG="$BENCH_STATLOG")   # 冒烟时 BENCH_STATLOG=8 看法力变化证明施法成功 | BENCH_STATLOG=8 in the smoke run: mana changes prove casts land
FXENV=(); [ "$FX" = keep ] || FXENV=(DEVICECTL_CHILD_DM_METALFX="$FX")
# BENCH_EXTRA_ENV="DM_FPS=120 DM_PRESENT_HZ=120 DM_3D_SCALE=0.5"：原样透传给 App（空格分隔的 名=值），做 120 Hz 之类的实验 | passed through to the App as-is (space-separated name=value), for experiments such as 120 Hz
for kv in ${BENCH_EXTRA_ENV:-}; do FXENV+=("DEVICECTL_CHILD_$kv"); done
# 启动前在锁内再确认一次（上面推存档要时间） | Re-check inside the lock right before launch (pushing the save takes time)
PID=$("${BD[@]}" pid "$D" "$B") || { t "🔴 读不到设备状态，不启动"; exit 1; }
[ -z "$PID" ] || { t "🔴 目标 App 刚被启动（进程 ${PID}），放弃本次基准，不结束它"; exit 1; }
env ${FXENV[@]+"${FXENV[@]}"} ${CASTENV[@]+"${CASTENV[@]}"} DEVICECTL_CHILD_DM_SAVE_TEST=1 DEVICECTL_CHILD_DM_TEST_ICLOUD_OFF=1 DEVICECTL_CHILD_DM_TEST_GOD=1 DEVICECTL_CHILD_DM_TEST_BRIGHTNESS="${BENCH_BRIGHTNESS:-0.5}" DEVICECTL_CHILD_DM_GAME_SEED=0x5a17 DEVICECTL_CHILD_DM_KEYS="$K" \
  DEVICECTL_CHILD_DM_TEST_WARP_LV="$WARP" DEVICECTL_CHILD_DM_TEST_SPAWN="$SPAWN" DEVICECTL_CHILD_DM_TEST_RENDER_RES="$SCHED" \
  DEVICECTL_CHILD_DM_HEAT="bench_$RUN/heat.tsv,5" DEVICECTL_CHILD_DM_THRCPU="bench_$RUN/thrcpu.tsv,10" DEVICECTL_CHILD_DM_SOAK="bench_$RUN/soak.tsv,30" \
  DEVICECTL_CHILD_DM_NVN_PASSGPU=0 DEVICECTL_CHILD_DM_WATCH=$((TOTAL + 240)) DEVICECTL_CHILD_DM_BENCH_DIR="bench_$RUN" \
  xcrun devicectl device process launch --device "$D" "$B" -mode boot --json-output "$OUT/launch.json" > "$OUT/launch.log" 2>&1 || { t "🔴 启动失败"; tail -3 "$OUT/launch.log"; exit 1; }
# 记下本次启动的进程号与时刻：收尾只结束这个实例（复审 F02）。App 自身在 DM_WATCH = 总时长 + 240 秒时退出，所以超过这个时间仍在的同号进程不可能是本次实例
# Record this launch's pid and time: the wrap-up ends only this instance (re-audit F02). The App exits by itself at DM_WATCH = run time + 240 s, so a same-pid process alive after that cannot be this instance
LPID=$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1]))["result"]["process"]["processIdentifier"])' "$OUT/launch.json" 2>/dev/null || true); LAUNCH_T=$(date +%s)
[ -n "$LPID" ] || t "⚠ 没读到本次启动的进程号：收尾不会结束任何进程（App 会在约 $(( (TOTAL + 240) / 60 )) 分钟后自行退出）"
printf '%s\n' "run=$RUN" "god=1" "slot=$SLOT" "brightness=${BENCH_BRIGHTNESS:-0.5}" "cast=$CAST" "stat=$STAT" "extra=${BENCH_EXTRA_ENV:-}" "seg_s=$SEGS" "levels=$LEVELS" "metalfx=$FX" "schedule=$SCHED" "warp=$WARP" "spawn=$SPAWN" "walk_s=$WALK" "launch_pid=${LPID}" > "$OUT/params.txt"
t "已启动：档位 ${LEVELS}，每段 ${SEG} 分钟，共 $((TOTAL / 60)) 分钟；MetalFX=${FX}。现在请拔掉数据线，屏幕保持亮着（自动锁定设为永不；电量按每小时约 35% 预估要够用）。"
sleep $((TOTAL + 60))
t "时间到。等设备重新接上…"
w=0; until present; do sleep 30; w=$((w + 30)); [ $w -ge 43200 ] && { t "🔴 等了 12 小时设备还没接回来"; exit 1; }; done   # 可以隔夜：第二天接回线再拉 | May run overnight: plug back in the next day and the data is pulled then
sleep 5
# 收尾：只结束本次启动的实例（复审 F02）。进程号对得上、且仍在 App 自身的寿命内才结束；对不上的同 App 进程（用户后来自己打开的）一律不动
# Wrap-up: end only this launch's instance (re-audit F02): only when the pid matches and we are still within the App's own lifetime; any other process of the same App (one the user opened later) is left alone
NOW=$("${BD[@]}" pids "$D" "$B") || { NOW=""; t "🔴 读不到目标 App 进程，不结束任何进程"; }
AGE=$(( $(date +%s) - LAUNCH_T ))
for p in $NOW; do
  if [ -n "$LPID" ] && [ "$p" = "$LPID" ] && [ "$AGE" -le $((TOTAL + 300)) ]; then
    xcrun devicectl device process terminate --device "$D" --pid "$p" > /dev/null 2>&1 && t "已结束本次基准启动的进程 ${p}" || t "⚠ 结束进程 ${p} 失败"
  else
    t "目标 App 进程 ${p} 不是本次基准启动的实例（本次 ${LPID:-未知}，已过 ${AGE} 秒），不结束"
  fi
done
sleep 3
# 拉数据：heat.tsv 与 boot.txt 是报告的输入，缺一个整个基准算失败；thrcpu / soak 只是附加读数（复审 F07）
# Pull: heat.tsv and boot.txt feed the report, missing either fails the whole run; thrcpu / soak are supplementary (re-audit F07)
MISSING=""
for f in heat.tsv thrcpu.tsv soak.tsv; do
  xcrun devicectl device copy from --device "$D" --domain-type appDataContainer --domain-identifier "$B" --source "Documents/bench_$RUN/$f" --destination "$OUT/$f" > /dev/null 2>&1 || { t "🔴 拉 ${f} 失败"; [ "$f" = heat.tsv ] && MISSING="$MISSING $f"; }
done
xcrun devicectl device copy from --device "$D" --domain-type appDataContainer --domain-identifier "$B" --source Documents/boot.txt --destination "$OUT/boot.txt" > /dev/null 2>&1 || { t "🔴 拉 boot.txt 失败"; MISSING="$MISSING boot.txt"; }
[ -z "$MISSING" ] || { t "🔴 关键数据没拉全（缺${MISSING}），本次基准失败；已拉到的在 ${OUT}"; exit 1; }
t "数据已拉回 ${OUT}"
python3 -I tools/bench_report.py "$OUT" > "$OUT/report.md"; RC=$?
cat "$OUT/report.md"
[ "$RC" = 0 ] || { t "🔴 报告生成失败（退出码 ${RC}），report.md 不完整"; exit 1; }

}
power_bench_main "$@"
