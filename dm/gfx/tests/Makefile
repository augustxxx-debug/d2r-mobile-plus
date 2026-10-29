# 单测：make 编译并运行（秒级）
# 源文件按通配纳入，口径与宿主 Makefile 的 GFX 一致（hle_nvn.c 由 t_nvn.c 直接 #include，故剔除）；新增源文件无需改这里
# Unit tests: make compiles and runs them (seconds)
# Source files are included by wildcard, same convention as GFX in the host Makefile (hle_nvn.c is #included directly by t_nvn.c, so it is excluded); adding new source files needs no change here
CFLAGS=-O1 -g -Wall -Wno-unused-function -I../nvn/include
GFX=$(filter-out ../nvn/hle_nvn.c,$(sort $(wildcard ../nvn/*.c ../nvn/*.m ../nvn/*.S ../tex/*.c ../tex/*.m)))
GFX_H=$(wildcard ../nvn/*.h ../nvn/*.inc ../nvn/include/*.h ../tex/*.h)
FW=-framework Metal -weak_framework MetalFX -framework Foundation -framework QuartzCore -framework ImageIO -framework CoreGraphics -framework UniformTypeIdentifiers
# 宿主 crash.c 一并链入：写保护单测走生产同一套缺页钩子
# Host crash.c is linked in as well: the write-protect unit tests go through the same page-fault hook as production
t_nvn: t_nvn.c ../nvn/hle_nvn.c ../../host/crash.c $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_nvn.c ../../host/crash.c $(GFX) $(FW)
t_dirty_slots: t_dirty_slots.c ../nvn/nvn_dirty.c ../../host/crash.c ../nvn/nvn_dirty.h
	clang $(CFLAGS) -o $@ t_dirty_slots.c ../../host/crash.c ../nvn/nvn_dirty.c
t_dirty_race: t_dirty_race.c ../nvn/nvn_dirty.c ../../host/crash.c ../nvn/nvn_dirty.h
	clang $(CFLAGS) -o $@ t_dirty_race.c ../../host/crash.c
t_rtshadow: t_rtshadow.m ../nvn/nvn_rtshadow_msl.h ../nvn/nvn_rtshadow_util.h
	clang -O1 -g -Wall -fobjc-arc -o $@ t_rtshadow.m -framework Metal -framework Foundation
t_rts_upsample: t_rts_upsample.m ../nvn/nvn_rtshadow_msl.h ../nvn/nvn_rtshadow_util.h
	clang -O1 -g -Wall -fobjc-arc -o $@ t_rts_upsample.m -framework Metal -framework Foundation
t_builtin: t_builtin.m ../nvn/nvn_builtin.h
	clang -O1 -g -Wall -fobjc-arc -o $@ t_builtin.m -framework Metal -weak_framework MetalFX -framework Foundation
t_deswz_gpu: t_deswz_gpu.m ../tex/nvn_deswizzle.c ../tex/nvn_deswizzle_gpu.m ../tex/nvn_deswizzle.h ../tex/nvn_deswizzle_gpu.h ../nvn/nvn_tex.c ../nvn/nvn_tex.h
	clang $(CFLAGS) -fobjc-arc -o $@ t_deswz_gpu.m ../tex/nvn_deswizzle.c ../tex/nvn_deswizzle_gpu.m ../nvn/nvn_tex.c -framework Metal -weak_framework MetalFX -framework Foundation
t_refl: t_refl.m ../nvn/nvn_refl.m ../nvn/nvn_refl.h ../nvn/dmsh_chain.h
	clang -O1 -g -Wall -fobjc-arc -o $@ t_refl.m ../nvn/nvn_refl.m -framework Metal -weak_framework MetalFX -framework Foundation
t_hmap: t_hmap.c ../nvn/nvn_hmap.h
	clang -O1 -Wall -o $@ t_hmap.c
t_cover: t_cover.c ../nvn/nvn_cover.h
	clang -O1 -Wall -o $@ t_cover.c
t_pace: t_pace.c ../nvn/nvn_pace.h
	clang -O1 -Wall -o $@ t_pace.c
t_pqdone: t_pqdone.c ../nvn/nvn_pqdone.h
	clang -O1 -Wall -o $@ t_pqdone.c
# 显示节拍诊断环的 GPU 完成时刻（五审 Q5-03）：普通一遍 + ThreadSanitizer 一遍 | GPU completion time of the display link diagnostic ring (fifth review Q5-03): a plain pass + a ThreadSanitizer pass
t_ltring: t_ltring.c ../nvn/nvn_ltring.h
	clang -O1 -g -Wall -o $@ t_ltring.c
t_rts_gpustat_tsan: t_rts_gpustat.c ../nvn/nvn_rt_gpustat.h   # 七审 Q7-04 | seventh review Q7-04
	clang -O1 -g -Wall -fsanitize=thread -o $@ t_rts_gpustat.c
t_ltring_tsan: t_ltring.c ../nvn/nvn_ltring.h
	clang -O1 -g -Wall -fsanitize=thread -o $@ t_ltring.c
t_adaptive_pace: t_adaptive_pace.c ../nvn/nvn_adaptive_pace.h ../nvn/nvn_pace.h
	clang -O1 -Wall -o $@ t_adaptive_pace.c
t_completion_wait: t_completion_wait.cpp ../vulkan/engine/xbg_completion_wait.h
	clang++ -std=c++17 -O1 -Wall -pthread -DDM_COMPLETION_WAIT_SELFTEST -DDM_COMPLETION_WAIT_MAIN -o $@ t_completion_wait.cpp
t_fuse_check: t_fuse_check.m ../../host/crash.c $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_fuse_check.m ../../host/crash.c $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_rt_srgb_view: t_rt_srgb_view.m ../../host/crash.c $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_rt_srgb_view.m ../../host/crash.c $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_present_crop: t_present_crop.m ../nvn/hle_nvn.c ../../host/crash.c $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_present_crop.m ../../host/crash.c $(filter-out ../nvn/nvn_mtl.m,$(GFX)) $(FW)
t_reorder_chain: t_reorder_chain.m ../../host/crash.c $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_reorder_chain.m ../../host/crash.c $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_defer_chain: t_defer_chain.m ../../host/crash.c $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_defer_chain.m ../../host/crash.c $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_zskip: t_zskip.m ../../host/crash.c $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_zskip.m ../../host/crash.c $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_xv: t_xv.m ../../host/crash.c $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_xv.m ../../host/crash.c $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_ub: t_ub.m ../../host/crash.c $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_ub.m ../../host/crash.c $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_mip_extent: t_mip_extent.m ../nvn/hle_nvn.c ../../host/crash.c $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_mip_extent.m ../../host/crash.c $(filter-out ../nvn/nvn_mtl.m,$(GFX)) $(FW)
t_rt_range_shape: t_rt_range_shape.m ../../host/crash.c $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_rt_range_shape.m ../../host/crash.c $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_rt_range_ro: t_rt_range_ro.m ../../host/crash.c $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_rt_range_ro.m ../../host/crash.c $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_rt_range: t_rt_range.m ../../host/crash.c $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_rt_range.m ../../host/crash.c $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_rt_depth_ro: t_rt_depth_ro.m ../../host/crash.c $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_rt_depth_ro.m ../../host/crash.c $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
# 光追阴影网格键的读取跨度（五审 Q5-01）：普通一遍 + AddressSanitizer 一遍 | Read span of the ray-traced shadow mesh key (fifth review Q5-01): a plain pass + an AddressSanitizer pass
t_rts_span: t_rts_span.m ../../host/crash.c $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_rts_span.m ../../host/crash.c $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_rts_span_asan: t_rts_span.m ../../host/crash.c $(GFX) $(GFX_H)
	clang $(CFLAGS) -fsanitize=address -fobjc-arc -o $@ t_rts_span.m ../../host/crash.c $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_rts_flicker: t_rts_flicker.m ../../host/crash.c $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_rts_flicker.m ../../host/crash.c $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
# 光追阴影网格的生命周期与就绪闸门（五审 Q5-02） | Lifecycle of ray-traced shadow meshes and the readiness gate (fifth review Q5-02)
t_rts_state: t_rts_state.m ../../host/crash.c $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_rts_state.m ../../host/crash.c $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
# 光追阴影六审剩余风险：当帧回退、槽的 GPU 生命周期、缓存预算与关闭释放 | Sixth-review remaining risks: same-frame fallback, GPU lifetime of slots, cache budget and release on switch-off
t_gcap: t_gcap.m ../../host/crash.c $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_gcap.m ../../host/crash.c $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
# Metal 抓帧层与常驻内存（光追内存 P1）：抓帧层关着时整帧光追常驻不涨，开着时逐帧上涨 | Metal capture layer and resident memory (ray-tracing memory P1): with the capture layer off whole ray-traced frames do not grow resident memory, with it on they do
# 紫闪探针网格「连片」判据 | the magenta probe grid's "contiguous" criterion
t_mp_grid: t_mp_grid.c ../nvn/nvn_mp_grid.h
	clang $(CFLAGS) -o $@ t_mp_grid.c
t_capmem: t_capmem.m ../../host/crash.c $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_capmem.m ../../host/crash.c $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
# 延迟释放真正销毁（RC1 光追关真机长测：退役未销毁缓冲 416 MB） | deferred release really destroys (RC1 RT-off device long test: 416 MB of retired but undestroyed buffers)
t_defer_drain: t_defer_drain.m ../../host/crash.c $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_defer_drain.m ../../host/crash.c $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_rts_gate: t_rts_gate.m ../../host/crash.c $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_rts_gate.m ../../host/crash.c $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_pso_skey: t_pso_skey.m ../nvn/nvn_pso_skey.h ../nvn/nvn_obj.h
	clang $(CFLAGS) -fobjc-arc -o $@ t_pso_skey.m -framework Foundation
t_pfv: t_pfv.m ../../host/crash.c $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_pfv.m ../../host/crash.c $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_view_retire: t_view_retire.m ../../host/crash.c $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_view_retire.m ../../host/crash.c $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_cmdalloc: t_cmdalloc.m ../../host/crash.c $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_cmdalloc.m ../../host/crash.c $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
t_memwarn: t_memwarn.m ../../host/crash.c $(GFX) $(GFX_H)
	clang $(CFLAGS) -fobjc-arc -o $@ t_memwarn.m ../../host/crash.c $(filter-out ../nvn/nvn_mtl.m ../nvn/nvn_entry.S ../nvn/nvn_thunks.S,$(GFX)) $(FW)
# 依赖一行一个、按字母序（免得加一个测试就整行冲突）；下面各条命令的先后不变
# Dependencies one per line in alphabetical order (so that adding one test does not conflict on the whole line); the order of the commands below is unchanged
run: \
	t_adaptive_pace \
	t_completion_wait \
	t_copy_pitch \
	t_builtin \
	t_cover \
	t_defer_chain \
	t_deswz_gpu \
	t_dirty_race \
	t_dirty_slots \
	t_fuse_check \
	t_hmap \
	t_ltring \
	t_ltring_tsan \
	t_rts_gpustat_tsan \
	t_mip_extent \
	t_nvn \
	t_pace \
	t_pfv \
	t_pqdone \
	t_present_crop \
	t_pso_skey \
	t_refl \
	t_reorder_chain \
	t_capmem \
	t_defer_drain \
	t_view_retire \
	t_cmdalloc \
	t_memwarn \
	t_mp_grid \
	t_gcap \
	t_rts_gate \
	t_rts_flicker \
	t_rts_span \
	t_rts_span_asan \
	t_rts_state \
	t_rts_upsample \
	t_rtshadow \
	t_rt_depth_ro \
	t_rt_range \
	t_rt_range_ro \
	t_rt_range_shape \
	t_rt_srgb_view \
	t_ub \
	t_xv \
	t_zskip
	./t_copy_pitch
	./t_nvn
	./t_dirty_slots
	./t_dirty_race
	./t_builtin
	./t_deswz_gpu
	./t_pace
	./t_pqdone
	./t_ltring
	TSAN_OPTIONS=halt_on_error=1 ./t_ltring_tsan
	TSAN_OPTIONS=halt_on_error=1 ./t_rts_gpustat_tsan
	./t_adaptive_pace
	./t_completion_wait
	./t_refl
	./t_hmap
	./t_cover
	./t_fuse_check
	./t_present_crop
	MTL_DEBUG_LAYER=1 ./t_reorder_chain
	MTL_DEBUG_LAYER=1 ./t_defer_chain
	./t_zskip
	./t_xv
	./t_ub
	MTL_DEBUG_LAYER=1 ./t_mip_extent
	MTL_DEBUG_LAYER=1 ./t_mip_extent --stencil-only
	./t_rt_depth_ro
	./t_rt_range_shape
	./t_rt_range_ro
	DM_NVN_UNWRITTEN_DC=0 MTL_DEBUG_LAYER=1 ./t_rt_range
	MTL_DEBUG_LAYER=1 ./t_rt_range   # DontCare 缺省开：附件字节按「每通道省一次深度模板写回」闭式期望 | DontCare on by default: attachment bytes use the closed-form expectation of "one depth-stencil store saved per pass"
	DM_NVN_UNWRITTEN_DC=0 MTL_DEBUG_LAYER=1 ./t_rt_range --disabled
	DM_NVN_UNWRITTEN_DC=0 MTL_DEBUG_LAYER=1 ./t_rt_range --small-depth
	DM_NVN_UNWRITTEN_DC=0 MTL_DEBUG_LAYER=1 ./t_rt_range --sample-attachment --both-flags
	./t_pso_skey
	MTL_DEBUG_LAYER=1 ./t_pfv
	MTL_DEBUG_LAYER=1 ./t_rt_srgb_view
	@./t_capmem; s=$$?; if [ $$s = 77 ]; then echo "SKIP t_capmem"; elif [ $$s != 0 ]; then exit $$s; fi   # 抓帧层与常驻内存（自己起两个子进程，一个带 METAL_CAPTURE_ENABLED）；77 = 机器不支持光追 | capture layer and resident memory (starts two children itself, one with METAL_CAPTURE_ENABLED); 77 = no ray tracing
	./t_defer_drain   # 延迟释放在客体线程上也真销毁 | deferred release destroys even on the guest thread
	./t_view_retire   # 贴图号视图缓存在基贴图退役时即时作废 | the texture-ID view cache is invalidated as soon as its base texture retires
	./t_memwarn   # 系统内存告警累计不封顶，进每 300 帧内存行 | uncapped system memory-warning total on the every-300-frames memory line
	@for a in "" --off; do ./t_cmdalloc $$a; s=$$?; if [ $$s = 77 ]; then echo "SKIP t_cmdalloc"; break; elif [ $$s != 0 ]; then exit $$s; fi; done   # 命令分配器超阈值换新；--off 证明分得出；77 = 旧接口版 | command allocators replaced above a threshold; --off proves it discriminates; 77 = older-API build
	./t_gcap   # GPU 抓帧（DM_GPU_CAPTURE）；测试自己设 METAL_CAPTURE_ENABLED | GPU capture (DM_GPU_CAPTURE); the test sets METAL_CAPTURE_ENABLED itself
	@for t in t_rts_gate t_rts_flicker t_rts_span t_rts_span_asan t_rts_state t_rts_upsample t_rtshadow; do MTL_DEBUG_LAYER=1 ./$$t; s=$$?; if [ $$s = 77 ]; then echo "SKIP $$t"; elif [ $$s != 0 ]; then exit $$s; fi; done   # 77 = 机器不支持光追 / 旧接口版 | 77 = no ray tracing on this machine / older-API build
	python3 test_tbdr_budget.py
	./t_mp_grid
	python3 test_present_line.py   # 上屏行全程带 footprint（真机长测 7500 帧后断） | the present line carries footprint throughout (cut after frame 7500 in the device long test)
.PHONY: run
# 旧接口版那一遍（图形后端编译期开关 DM_NVN_M3=1，目标 macOS 15.0）：在旁边的 tests_m3 目录里用同一份测试源码另编另跑，不动这里的缺省产物。
# 闸门（verify/accept.sh）两遍都跑，免得以后只测一边
# The older-API pass (compile-time switch DM_NVN_M3=1 of the graphics backend, target macOS 15.0): built and run separately from the same test sources in the sibling tests_m3 directory, leaving the default outputs here untouched.
# The gate (verify/accept.sh) runs both passes, so that only one side being tested cannot happen later
run-m3:
	rm -rf ../tests_m3 && mkdir ../tests_m3 && cp Makefile *.c *.cpp *.m *.py ../tests_m3/
	$(MAKE) -C ../tests_m3 run CFLAGS="$(CFLAGS) -DDM_NVN_M3=1 -mmacosx-version-min=15.0"

t_copy_pitch: t_copy_pitch.c ../nvn/nvn_copy_pitch.h
	clang -O1 -Wall -Werror -o $@ t_copy_pitch.c

# 独立超分检查：合成颜色与遮罩，新旧两种后端分别运行。
# Standalone upscaling checks: synthetic colors/mask, run each backend separately.
t_metalfx: t_metalfx.m ../nvn/nvn_metalfx.m ../nvn/nvn_metalfx.h
	clang -O1 -fobjc-arc -mmacosx-version-min=26.0 -o $@ t_metalfx.m ../nvn/nvn_metalfx.m -framework Metal -framework MetalFX -framework Foundation
t_metalfx_m3: t_metalfx.m ../nvn/nvn_metalfx.m ../nvn/nvn_metalfx.h
	clang -O1 -fobjc-arc -DDM_NVN_M3=1 -mmacosx-version-min=15.0 -o $@ t_metalfx.m ../nvn/nvn_metalfx.m -framework Metal -framework MetalFX -framework Foundation

# Run with API validation: successful compilation does not prove synchronization.
METALFX_CHECKS = $(if $(findstring -DDM_NVN_M3=1,$(CFLAGS)),t_metalfx_m3,t_metalfx t_metalfx_m3)
.PHONY: check-metalfx
check-metalfx: $(METALFX_CHECKS)
	@for test in $(METALFX_CHECKS); do MTL_DEBUG_LAYER=1 ./$$test; status=$$?; if [ $$status -eq 77 ]; then echo "SKIP $$test: unsupported GPU/backend"; elif [ $$status -ne 0 ]; then exit $$status; fi; done
run: check-metalfx
