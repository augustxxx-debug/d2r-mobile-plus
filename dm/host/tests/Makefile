DM_ROOT ?= $(HOME)/dm-work
# dm/host 单测一条命令：make -C dm/host/tests run（秒级；产物放 build/ 不进仓）
# dm/host unit tests in one command: make -C dm/host/tests run (takes seconds; build output goes to build/ and is not committed)
PY ?= $(DM_ROOT)/build/cache/dm/venv/bin/python
EXEFS ?= $(DM_ROOT)/data/switch/3.3.94012/Update/ExeFS
B = build
MOVIE_PACKAGES = libavformat libavcodec libavutil libswscale
MOVIE_TEST_FLAGS ?= -O1
.PHONY: movie-real
# 依赖一行一个、按字母序（同 ../Makefile 的 SRC：免得加一个测试就整行冲突）；下面各条命令的先后不变
# Dependencies one per line in alphabetical order (same as SRC in ../Makefile: so that adding one test does not conflict on the whole line); the order of the commands below is unchanged
run: \
	$(B)/t_metalfx_handoff \
	$(B)/t_notice_dispatch \
	$(B)/t_notice_widgets \
	$(B)/t_modal_input_gate \
	$(B)/t_settings_modal_input \
	$(B)/t_metalfx_auto \
	$(B)/main.orig.bin \
	$(B)/subsdk0.orig.bin \
	$(B)/t_altstack \
	$(B)/t_audio \
	$(B)/t_audio_spsc \
	$(B)/t_cloudsync \
	$(B)/t_condvar \
	$(B)/t_condvar_old \
	$(B)/t_custom_readlog \
	$(B)/t_community \
	$(B)/t_crashfence \
	$(B)/t_doorspot \
	$(B)/t_fs \
	$(B)/t_fs_case \
	$(B)/t_hid_receipt \
	$(B)/t_hid_script \
	$(B)/t_hud_dead \
	$(B)/t_i18n \
	$(B)/t_minimap_lift \
	$(B)/t_loader_weak \
	$(B)/t_mac_overlay \
	$(B)/t_modetx \
	$(B)/t_movie \
	$(B)/t_namegen \
	$(B)/t_notice_geometry \
	$(B)/t_oe \
	$(B)/t_render_profile \
	$(B)/t_graphics_gate \
	$(B)/t_res_auto_policy \
	$(B)/t_res_cap \
	$(B)/t_custom_scroll \
	$(B)/t_hud_mask \
	$(B)/t_uiscreen_frontend \
	$(B)/t_padmap \
	$(B)/t_pouch_notice \
	$(B)/t_rift_clock \
	$(B)/t_rift_next \
	$(B)/t_rift_oldrun \
	$(B)/t_rift_wave \
	$(B)/t_interaction_diag \
	$(B)/t_map_diag \
	$(B)/t_warp_diag \
	$(B)/t_romfs_validate \
	$(B)/t_rwlv \
	$(B)/t_swkbd \
	$(B)/t_target_rank \
	$(B)/t_textbox \
	$(B)/t_thread_leak \
	$(B)/t_touch \
	$(B)/t_touch_mac \
	$(B)/t_unit_level \
	$(B)/t_widget
	python3 native_write_scan.py --selftest ..
	python3 widget_cache_scan.py --selftest ..
	python3 mode_scan.py ../..
	python3 ../../ios/tools/dm_i18n.py --check
	python3 test_literal_scan.py
	python3 ../../i18n/scan_literals.py
	$(B)/t_thread_leak
	$(B)/t_loader_weak
	$(B)/t_mac_overlay
	$(PY) test_mac_features.py
	$(B)/t_rift_clock
	$(B)/t_rift_next
	$(B)/t_rift_oldrun
	$(B)/t_interaction_diag
	$(B)/t_map_diag
	$(B)/t_warp_diag
	$(B)/t_rift_wave
	$(B)/t_textbox
	$(B)/t_rwlv
	$(B)/t_touch
	$(B)/t_hid_script 2>/dev/null
	$(B)/t_hid_receipt 2>/dev/null
	$(B)/t_touch_mac
	$(B)/t_oe 2>/dev/null
	$(B)/t_metalfx_auto
	$(B)/t_metalfx_handoff
	$(B)/t_metalfx_handoff --disabled
	$(B)/t_render_profile
	$(B)/t_graphics_gate
	$(B)/t_custom_readlog
	$(B)/t_crashfence 2>/dev/null
	$(B)/t_community 2>/dev/null
	$(B)/t_res_cap
	$(B)/t_custom_scroll
	$(B)/t_hud_mask
	$(B)/t_res_auto_policy 2>/dev/null
	$(B)/t_pouch_notice
	$(B)/t_modal_input_gate
	$(B)/t_settings_modal_input
	$(B)/t_notice_geometry
	$(B)/t_notice_dispatch
	$(B)/t_notice_widgets
	$(B)/t_uiscreen_frontend
	$(B)/t_fs 2>/dev/null
	TMPDIR="$(abspath $(B))" $(B)/t_fs_case 2>/dev/null
	DM_ROMFS="$(DM_ROOT)/data/switch/3.3.94012/Update/RomFS" $(B)/t_romfs_validate
	$(B)/t_audio
	$(B)/t_audio_spsc
	$(B)/t_swkbd
	$(B)/t_altstack
	$(B)/t_movie
	$(B)/t_condvar
	$(B)/t_condvar_old
	$(B)/t_target_rank 2>/dev/null
	$(B)/t_hud_dead
	$(B)/t_namegen
	$(B)/t_minimap_lift
	$(B)/t_modetx
	$(B)/t_padmap
	$(B)/t_unit_level
	$(B)/t_widget
	$(B)/t_doorspot
	TMPDIR="$(abspath $(B))" $(B)/t_cloudsync 2>/dev/null
	for m in en zh none sysen syses systw sysru sysko tags game; do TMPDIR="$(abspath $(B))" $(B)/t_i18n $$m || exit 1; done
	$(PY) movie_cut.py $(B)/main.orig.bin $(B)/subsdk0.orig.bin
	$(PY) oe_notify.py $(B)/main.orig.bin
	$(PY) notice_queue.py $(B)/main.orig.bin
$(B)/t_i18n: t_i18n.c ../dm_i18n.c ../dm_i18n.h ../dm_i18n_tab.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_i18n.c ../dm_i18n.c
$(B)/t_mac_overlay: t_mac_overlay.m ../app_mac_overlay.h | $(B)
	clang -O1 -Wall -Werror -fobjc-arc -framework Foundation -o $@ t_mac_overlay.m
$(B)/t_hud_dead: t_hud_dead.c ../dm_hud.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_hud_dead.c
$(B)/t_textbox: t_textbox.c ../dm_textbox_core.c ../dm_textbox_core.h | $(B)
	clang -O1 -Wall -Werror -o $@ t_textbox.c ../dm_textbox_core.c
$(B)/t_rwlv: t_rwlv.c ../dm_rwlv_core.c ../dm_rwlv_core.h | $(B)
	clang -O1 -Wall -Werror -o $@ t_rwlv.c ../dm_rwlv_core.c
$(B)/t_doorspot: t_doorspot.c ../dm_doorspot.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_doorspot.c
$(B)/t_widget: t_widget.c ../dm_widget.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_widget.c
$(B)/t_unit_level: t_unit_level.c ../dm_unit.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_unit_level.c
$(B)/t_padmap: t_padmap.c ../dm_padmap.h ../dm_input.h | $(B)
	clang -O1 -Wall -Werror -o $@ t_padmap.c
$(B)/t_minimap_lift: t_minimap_lift.c ../dm_cursor.c ../dm_cursor.h | $(B)
	clang -O1 -Wall -Werror -Wno-unused-function -I.. -Wl,-dead_strip -o $@ t_minimap_lift.c
$(B)/t_modetx: t_modetx.c ../dm_modetx.h | $(B)
	clang -O1 -Wall -Werror -o $@ t_modetx.c
$(B)/t_namegen: t_namegen.c ../dm_namegen.c | $(B)
	clang -O1 -Wall -Werror -o $@ t_namegen.c ../dm_namegen.c
$(B)/t_cloudsync: t_cloudsync.c ../dm_cloudsync.c ../dm.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_cloudsync.c ../dm_cloudsync.c
$(B)/t_touch: t_touch.c ../hle_touch.c ../front.h ../dm.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_touch.c ../hle_touch.c
$(B)/t_fs: t_fs.c t_fs_stub.c ../hle_fs.c ../hle.h ../dm.h ../dm_i18n.c | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_fs.c t_fs_stub.c ../dm_i18n.c
$(B)/t_audio: t_audio.c ../hle_audio.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_audio.c -framework AudioToolbox -framework CoreAudio -framework CoreFoundation
$(B)/t_audio_spsc: t_audio_spsc.c ../hle_audio.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_audio_spsc.c -framework AudioToolbox -framework CoreAudio -framework CoreFoundation
$(B)/t_audio_spsc_tsan: t_audio_spsc.c ../hle_audio.c ../hle.h ../dm.h | $(B)
	clang -O1 -g -O1 -fsanitize=thread -Wno-unused-function -I.. -o $@ t_audio_spsc.c -framework AudioToolbox -framework CoreAudio -framework CoreFoundation
$(B)/t_swkbd: t_swkbd.c ../hle_swkbd.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_swkbd.c ../hle_swkbd.c
$(B)/t_movie: t_movie.c ../hle_movie.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_movie.c ../hle_movie.c
$(B)/t_movie_real: t_movie_real.c ../hle_movie.c ../dm_movie_decode.c ../dm_movie_decode.h ../hle.h ../dm.h | $(B)
	clang $(MOVIE_TEST_FLAGS) -Wall -Wno-unused-function -DDM_MOVIE_FFMPEG=1 -I.. -o $@ t_movie_real.c ../dm_movie_decode.c $(shell pkg-config --cflags --libs $(MOVIE_PACKAGES))
movie-real: $(B)/t_movie_real
	$(PY) movie_real.py $(B)/t_movie_real
$(B)/t_altstack: t_altstack.c t_altstack_stub.c ../crash.c ../dm.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_altstack.c t_altstack_stub.c ../crash.c
$(B)/%.orig.bin: $(EXEFS)/% | $(B)
	$(PY) ../../tools/nso.py $< $@ > /dev/null
$(B):
	mkdir -p $@
.PHONY: run tsan
tsan: $(B)/t_audio_spsc_tsan
	$(B)/t_audio_spsc_tsan
$(B)/t_condvar: t_condvar.c t_condvar_stub.c ../hle_thread.c ../dm.h | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -I.. -o $@ t_condvar.c t_condvar_stub.c ../hle_thread.c
$(B)/t_condvar_old: t_condvar.c t_condvar_stub.c ../hle_thread.c ../dm.h | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -DDM_TEST_CV_OLD_ORDER -I.. -o $@ t_condvar.c t_condvar_stub.c ../hle_thread.c

$(B)/t_fs_case: t_fs_case.c ../hle_fs.c ../hle.h ../dm.h ../dm_i18n.c | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_fs_case.c ../dm_i18n.c
$(B)/t_romfs_validate: t_romfs_validate.c ../romfs_validate.c ../romfs_validate.h ../dm_i18n.c ../dm_i18n_tab.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_romfs_validate.c ../romfs_validate.c ../dm_i18n.c

$(B)/t_hid_script: t_uiscreen_fixture.h t_hid_script.c t_touch_tick_stub.c ../hle_hid.c ../dm_cursor.c ../dm_hud.c ../dm_hud.h ../touch_mac.c ../../ios/Sources/dm_touch_controls.c ../hle.h ../dm.h ../dm_input.h ../front.h | $(B) ../dm_prof.c
	clang -O1 -Wno-unused-function -I.. -I../../ios/Sources -o $@ t_hid_script.c t_touch_tick_stub.c ../dm_cursor.c ../dm_hud.c ../touch_mac.c ../../ios/Sources/dm_touch_controls.c -lm ../dm_prof.c

$(B)/t_oe: t_oe.c ../hle_oe.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_oe.c

$(B)/t_uiscreen_frontend: t_uiscreen_frontend.c ../dm_uiscreen.c ../dm_uiscreen.h ../hle.h ../dm.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_uiscreen_frontend.c

$(B)/t_hid_receipt: t_uiscreen_fixture.h t_hid_receipt.c t_touch_tick_stub.c ../hle_hid.c ../dm_cursor.c ../dm_hud.c ../dm_hud.h ../touch_mac.c ../../ios/Sources/dm_touch_controls.c ../hle.h ../dm.h ../dm_input.h ../front.h | $(B) ../dm_prof.c
	clang -O1 -Wno-unused-function -I.. -I../../ios/Sources -o $@ t_hid_receipt.c t_touch_tick_stub.c ../dm_cursor.c ../dm_hud.c ../touch_mac.c ../../ios/Sources/dm_touch_controls.c -lm ../dm_prof.c
$(B)/t_touch_mac: t_uiscreen_fixture.h t_touch_mac.c ../touch_mac.c ../dm_cursor.c ../dm_hud.c ../dm_hud.h ../../ios/Sources/dm_touch_controls.c ../../ios/Sources/dm_touch_controls.h ../../host/dm_input.h | $(B)   # 测试 #include 了 touch_mac.c 与 dm_touch_controls.c，须列为依赖（漏列致改源不重编） ../dm_prof.c | The tests #include touch_mac.c and dm_touch_controls.c, so they must be listed as dependencies (missing them means editing the source does not trigger a rebuild) ../dm_prof.c
	clang -std=c11 -Wall -Wextra -Werror -Wno-unused-function -I.. -I../../ios/Sources -o $@ t_touch_mac.c ../dm_cursor.c ../dm_hud.c -lm ../dm_prof.c
$(B)/t_target_rank: t_target_rank.c ../hle_target.c ../dm.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_target_rank.c ../hle_target.c
$(B)/t_rift_clock: t_rift_clock.c ../dm_rift.c ../dm_rift.h ../hle_os.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -ffunction-sections -fdata-sections -Wl,-dead_strip -I.. -o $@ t_rift_clock.c ../hle_os.c
$(B)/t_rift_next: t_rift_next.c ../dm_rift.c ../dm_rift.h ../hle_os.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -ffunction-sections -fdata-sections -Wl,-dead_strip -I.. -o $@ t_rift_next.c ../hle_os.c
$(B)/t_rift_oldrun: t_rift_oldrun.c ../dm_rift.c ../dm_rift.h ../hle_os.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -ffunction-sections -fdata-sections -Wl,-dead_strip -I.. -o $@ t_rift_oldrun.c ../hle_os.c
$(B)/t_rift_wave: t_rift_wave.c ../dm_rift.c ../dm_rift.h ../hle_os.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -ffunction-sections -fdata-sections -Wl,-dead_strip -I.. -o $@ t_rift_wave.c ../hle_os.c
$(B)/t_thread_leak: t_thread_leak.c ../thread.c ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -I../../ios/Sources -o $@ t_thread_leak.c

$(B)/t_interaction_diag: t_interaction_diag.c ../dm_world.c ../dm_unit.h ../touch_mac.h | $(B)
	clang -O1 -Wno-unused-function -Wl,-dead_strip -I.. -o $@ t_interaction_diag.c

$(B)/t_loader_weak: t_loader_weak.c ../loader.c ../hle.c ../dm.h ../hle.h | $(B)
	clang -O1 -Wno-deprecated-declarations -I.. -Wl,-dead_strip -o $@ t_loader_weak.c ../hle.c

$(B)/t_map_diag: t_map_diag.c ../dm_cursor.c ../dm_unit.h | $(B)
	clang -O1 -Wno-unused-function -Wl,-dead_strip -I.. -o $@ t_map_diag.c

$(B)/t_warp_diag: t_warp_diag.c ../dm_rift.c ../dm_unit.h | $(B)
	clang -O1 -Wno-unused-function -Wl,-dead_strip -I.. -o $@ t_warp_diag.c

$(B)/t_render_profile: t_render_profile.c ../dm_custom.c ../dm_custom.h | $(B)
	clang -O1 -Wall -Werror -Wno-unused-function -Wl,-dead_strip -I.. -o $@ t_render_profile.c
$(B)/t_graphics_gate: t_graphics_gate.c ../dm_custom.c ../dm_custom.h | $(B)
	clang -O1 -Wall -Werror -Wno-unused-function -Wl,-dead_strip -I.. -o $@ t_graphics_gate.c
$(B)/t_custom_readlog: t_custom_readlog.c ../dm_custom.c ../dm_custom.h | $(B)
	clang -O1 -Wall -Werror -Wno-unused-function -Wl,-dead_strip -I.. -o $@ t_custom_readlog.c
$(B)/t_crashfence: t_crashfence.c ../dm_crashfence.c ../dm_crashfence.h ../dm_custom.c ../dm_custom.h | $(B)
	clang -O1 -Wall -Werror -Wno-unused-function -Wl,-dead_strip -I.. -o $@ t_crashfence.c
$(B)/t_community: t_community.c ../dm_community.c ../dm_community.h ../dm_custom.c ../dm_custom.h | $(B)
	clang -O1 -Wall -Werror -Wno-unused-function -Wl,-dead_strip -I.. -o $@ t_community.c

$(B)/t_res_auto_policy: t_res_auto_policy.c t_metalfx_handoff_backend.c ../hle_oe.c ../dm_res_cap.h ../hle.h ../dm.h | $(B)
	clang -O1 -Wall -Werror -Wno-unused-function -I.. -Wl,-dead_strip -o $@ t_res_auto_policy.c t_metalfx_handoff_backend.c

$(B)/t_res_cap: t_res_cap.c ../dm_res_cap.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_res_cap.c -lm
$(B)/t_custom_scroll: t_custom_scroll.c ../dm_custom_scroll.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_custom_scroll.c
$(B)/t_hud_mask: t_hud_mask.c ../dm_hud_mask.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_hud_mask.c

$(B)/t_pouch_notice: t_pouch_notice.c ../dm_pouch.c ../dm_widget.h ../hle.h | $(B)
	clang -O1 -Wall -Werror -Wno-unused-function -Wl,-dead_strip -I.. -o $@ t_pouch_notice.c

$(B)/t_notice_geometry: t_notice_geometry.c ../dm_notice_geometry.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_notice_geometry.c

$(B)/t_metalfx_auto: t_metalfx_auto.c ../../gfx/nvn/nvn_metalfx_policy.h | $(B)
	$(CC) -std=c11 -Wall -Wextra -Werror $< -o $@

$(B)/t_modal_input_gate: t_modal_input_gate.c ../dm_cursor.c ../dm_cursor.h | $(B)
	clang -O1 -Wall -Werror -Wno-unused-function -I.. -Wl,-dead_strip -o $@ $<
$(B)/t_settings_modal_input: t_settings_modal_input.c ../dm_custom.c ../dm_cursor.h ../dm_widget.h | $(B)
	clang -O1 -Wall -Werror -Wno-unused-function -I.. -Wl,-dead_strip -o $@ $<

$(B)/t_notice_widgets: t_notice_widgets.c ../dm_cursor.c ../dm_cursor.h ../dm_widget.h | $(B)
	clang -O1 -Wall -Werror -Wno-unused-function -I.. -Wl,-dead_strip -o $@ $<

$(B)/t_notice_dispatch: t_notice_dispatch.c ../dm_bossrush.c ../dm_bossrush.h | $(B)
	clang -O1 -Wall -Werror -Wno-unused-function -I.. -Wl,-dead_strip -o $@ t_notice_dispatch.c

$(B)/t_metalfx_handoff: t_metalfx_handoff.c t_metalfx_handoff_backend.c ../hle_oe.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wall -Werror -Wno-unused-function -I.. -Wl,-dead_strip -o $@ t_metalfx_handoff.c t_metalfx_handoff_backend.c
