#!/bin/bash
# 功耗基准（真机、无人值守）：一次启动跑完整个渲染分辨率档位矩阵，游戏内按脚本走动、轮换关卡、定时刷怪；每 5 秒记一行电量 / 温控 / 帧率 / GPU，
# 结束后拉回 Documents/bench_<run>/ 并生成报告。测的是整机电量下降速率（%/小时），电量只有 1% 分辨率，所以每段至少 10 分钟才有意义。
# 用法：dm/ios/tools/power_bench.sh <每段分钟> <档位序列如 1,3,0,2> [MetalFX: keep|off|auto|eco|balanced|quality（缺省 keep）] [存档备份目录（首次建 save.test 用）]
#   例：dm/ios/tools/power_bench.sh 12 1,3,0,2 keep "$DM_ROOT/run/save-backup/<时刻>/<子目录>"
# 流程：① 核对设备在、游戏没在跑；② Documents/save.test 不在就从备份目录推一份（绝不碰 Documents/save）；③ 带环境变量启动（测试存档、固定种子、按键脚本、
#   轮换关卡、刷怪、档位时间表、采样）；④ 提示拔线，等待 总时长+1 分钟；⑤ 等设备重新接上后拉回数据、停进程、出报告。
# 不装机：装机用 run.sh install。走设备锁（测量类）。产物：$DM_RUN_DIR 或 $DM_ROOT/run/ios-fable5/bench/<run>/
# Power benchmark (real device, unattended): one launch runs the whole render-resolution matrix; in-game the script walks, cycles through levels and spawns monsters on a timer;
# one row every 5 s with battery / thermal / fps / GPU, then results are pulled from Documents/bench_<run>/ and a report is written. Measures whole-device battery drain (%/hour);
# the battery level has 1% resolution, so each segment needs at least 10 minutes to mean anything.
# Usage: dm/ios/tools/power_bench.sh <minutes per segment> <level sequence e.g. 1,3,0,2> [MetalFX: keep|off|auto|eco|balanced|quality (default keep)] [save backup dir (to create save.test the first time)]
# 整个脚本体放在函数里再调用：bash 边读边执行，跑着的时候再编辑文件会把新内容当命令执行（2026-10-26 基线收尾因此失败）
# The whole body lives in a function that is called at the end: bash reads scripts incrementally, and editing the file while it runs executes the new text as commands (that is how the 2026-10-26 baseline lost its wrap-up)
power_bench_main() {
set -u
cd "$(dirname "$0")/.."
source tools/local_env.sh && local_env_need DM_DEVICE DM_BUNDLE_ID || exit 1
export DM_DEVBATTERY=0 DM_DEVLOCK_CLASS=measure
source tools/devlock.sh
SEG=${1:?每段分钟}; LEVELS=${2:?档位序列，如 1,3,0,2}; FX=${3:-keep}; SAVESRC=${4:-}
D=$DM_DEVICE; B=$DM_BUNDLE_ID; RUN=$(date +%Y%m%d-%H%M%S)
OUT=${DM_RUN_DIR:-${DM_ROOT:-$HOME/dm-work}/run/ios-fable5}/bench/$RUN; mkdir -p "$OUT"
t(){ echo "[$(date +%T)] $*" | tee -a "$OUT/bench.log"; }
alive(){ xcrun devicectl device info processes --device "$D" 2>/dev/null | grep -q "DmIOS.app/DmIOS"; }
present(){ xcrun devicectl list devices 2>/dev/null | grep -q "$D"; }
present || { t "🔴 设备没接上（devicectl 看不到 $D）"; exit 1; }
alive && { t "🔴 游戏正在运行：请先在游戏里保存退出并关闭 App，再跑基准"; exit 1; }
# ---- 测试存档：只用 Documents/save.test，缺了就从备份推一份 | Test save: only Documents/save.test; push one from the backup if missing
if ! xcrun devicectl device copy from --device "$D" --domain-type appDataContainer --domain-identifier "$B" --source Documents/save.test/Settings --destination "$OUT/.probe-settings" > /dev/null 2>&1; then
  [ -n "$SAVESRC" ] && [ -d "$SAVESRC/save" ] || { t "🔴 设备上没有 Documents/save.test，且没给存档备份目录（第 4 个参数，里面要有 save/）"; exit 1; }
  xcrun devicectl device copy to --device "$D" --domain-type appDataContainer --domain-identifier "$B" --source "$SAVESRC/save" --destination Documents/save.test > "$OUT/push_save.log" 2>&1 || { t "🔴 推测试存档失败（见 $OUT/push_save.log）"; exit 1; }
  t "已从备份建立 Documents/save.test（Documents/save 未动）"
fi
rm -rf "$OUT/.probe-settings"
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
devlock_acquire "power_bench $RUN" $((TOTAL + 1800)) || exit 1
trap devlock_release EXIT
env ${FXENV[@]+"${FXENV[@]}"} ${CASTENV[@]+"${CASTENV[@]}"} DEVICECTL_CHILD_DM_SAVE_TEST=1 DEVICECTL_CHILD_DM_TEST_ICLOUD_OFF=1 DEVICECTL_CHILD_DM_TEST_GOD=1 DEVICECTL_CHILD_DM_TEST_BRIGHTNESS="${BENCH_BRIGHTNESS:-0.5}" DEVICECTL_CHILD_DM_GAME_SEED=0x5a17 DEVICECTL_CHILD_DM_KEYS="$K" \
  DEVICECTL_CHILD_DM_TEST_WARP_LV="$WARP" DEVICECTL_CHILD_DM_TEST_SPAWN="$SPAWN" DEVICECTL_CHILD_DM_TEST_RENDER_RES="$SCHED" \
  DEVICECTL_CHILD_DM_HEAT="bench_$RUN/heat.tsv,5" DEVICECTL_CHILD_DM_THRCPU="bench_$RUN/thrcpu.tsv,10" DEVICECTL_CHILD_DM_SOAK="bench_$RUN/soak.tsv,30" \
  DEVICECTL_CHILD_DM_NVN_PASSGPU=0 DEVICECTL_CHILD_DM_WATCH=$((TOTAL + 240)) DEVICECTL_CHILD_DM_BENCH_DIR="bench_$RUN" \
  xcrun devicectl device process launch --device "$D" --terminate-existing "$B" -mode boot > "$OUT/launch.log" 2>&1 || { t "🔴 启动失败"; tail -3 "$OUT/launch.log"; exit 1; }
printf '%s\n' "run=$RUN" "god=1" "slot=$SLOT" "brightness=${BENCH_BRIGHTNESS:-0.5}" "cast=$CAST" "stat=$STAT" "extra=${BENCH_EXTRA_ENV:-}" "seg_s=$SEGS" "levels=$LEVELS" "metalfx=$FX" "schedule=$SCHED" "warp=$WARP" "spawn=$SPAWN" "walk_s=$WALK" > "$OUT/params.txt"
t "已启动：档位 $LEVELS，每段 $SEG 分钟，共 $((TOTAL / 60)) 分钟；MetalFX=$FX。现在请拔掉数据线，屏幕保持亮着（自动锁定设为永不；电量按每小时约 35% 预估要够用）。"
sleep $((TOTAL + 60))
t "时间到。等设备重新接上…"
w=0; until present; do sleep 30; w=$((w + 30)); [ $w -ge 43200 ] && { t "🔴 等了 12 小时设备还没接回来"; exit 1; }; done   # 可以隔夜：第二天接回线再拉 | May run overnight: plug back in the next day and the data is pulled then
sleep 5
p=$(xcrun devicectl device info processes --device "$D" 2>/dev/null | grep "DmIOS.app/DmIOS" | awk '{print $1}'); [ -n "$p" ] && xcrun devicectl device process terminate --device "$D" --pid "$p" > /dev/null 2>&1; sleep 3
for f in heat.tsv thrcpu.tsv soak.tsv; do xcrun devicectl device copy from --device "$D" --domain-type appDataContainer --domain-identifier "$B" --source "Documents/bench_$RUN/$f" --destination "$OUT/$f" > /dev/null 2>&1 || t "🔴 拉 $f 失败"; done
xcrun devicectl device copy from --device "$D" --domain-type appDataContainer --domain-identifier "$B" --source Documents/boot.txt --destination "$OUT/boot.txt" > /dev/null 2>&1 || t "🔴 拉 boot.txt 失败"
t "数据已拉回 $OUT"
python3 -I tools/bench_report.py "$OUT" | tee "$OUT/report.md"

}
power_bench_main "$@"
