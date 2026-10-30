// 统计上报的线上字段名与取值：全部集中在这里（看板 PRD §2.1 / §2.1.1；协议定稿后以 docs/TELEMETRY_PROTOCOL.md 为准，改这一个文件）。服务端拒收未知顶层键，所以这里没有的键一律不发。
// Wire field names and values of the usage statistics, all in one place (dashboard PRD 2.1 / 2.1.1; once final, docs/TELEMETRY_PROTOCOL.md rules: change this one file). The server refuses unknown top-level keys, so nothing outside this file is ever sent.
#pragma once

#define DM_TELEMETRY_SCHEMA 1
#define DM_TELEMETRY_CONSENT_VERSION 1   // 告知框版本；M1 / M2 新字段在 v1 文案范围内不升（PRD §2.0） | notice version; new M1 / M2 fields within the v1 text do not raise it

// 接口（外壳拼在 DMDiagnosticUploadOrigin 后面）| endpoints (appended to the origin by the shells)
#define DM_TF_API_BASE "telemetry/v1/"
#define DM_TF_API_CHALLENGE "challenge"
#define DM_TF_API_EVENTS "events"
#define DM_TF_TRANSCRIPT "D2R-TELEMETRY-V1"

// 事件类型 | event types
#define DM_TF_TYPE_LAUNCH "launch"
#define DM_TF_TYPE_SESSION_END "session_end"
#define DM_TF_TYPE_PERF_WINDOW "perf_window"

// 批的外层 | the batch envelope
#define DM_TF_BATCH_SCHEMA "schema"
#define DM_TF_BATCH_EVENTS "events"

// 公共头与启动事件 | common head and launch event
#define DM_TF_SCHEMA "schema"
#define DM_TF_EVENT_ID "event_id"
#define DM_TF_TYPE "type"
#define DM_TF_CLIENT_TS "client_ts"
#define DM_TF_SESSION_ID "session_id"
#define DM_TF_SEQ "seq"
#define DM_TF_MONO_S "mono_s"
#define DM_TF_PLATFORM "platform"
#define DM_TF_APP_VERSION "app_version"
#define DM_TF_BUILD "build"
#define DM_TF_SOURCE_REVISION "source_revision"
#define DM_TF_CHANNEL "channel"
#define DM_TF_PACK_SOURCE "pack_source"
#define DM_TF_MODEL "model"
#define DM_TF_SOC "soc"
#define DM_TF_GPU_DRIVER "gpu_driver"
#define DM_TF_OS_VERSION "os_version"
#define DM_TF_MEM_TIER "mem_tier"
#define DM_TF_MEM_LIMIT_MIB "mem_limit_mib"
#define DM_TF_DEVICE_CODE "device_code"
#define DM_TF_CONSENT_VERSION "consent_version"
#define DM_TF_LOCALE "locale"
#define DM_TF_PREV_SESSION_ID "prev_session_id"
#define DM_TF_PREV_EXIT "prev_exit"
#define DM_TF_PREV_EXIT_ARG "prev_exit_arg"
#define DM_TF_AUTO_CRASH "auto_crash"
#define DM_TF_QUEUE_DROPPED "queue_dropped"
#define DM_TF_SETTINGS "settings"

// prev_exit 取值 | prev_exit values
#define DM_TF_EXIT_NORMAL "normal"
#define DM_TF_EXIT_CRASH "crash"
#define DM_TF_EXIT_HANG "hang"
#define DM_TF_EXIT_MEM "mem"
#define DM_TF_EXIT_UNKNOWN "unknown"
#define DM_TF_EXIT_NONE "none"

// 设置快照：键名对齐 dm_sharelog.m perf_block「Enhanced preferences」与设置文件的键 | the settings snapshot: names follow perf_block's "Enhanced preferences" and the settings file's keys
#define DM_TF_SETTING_FLAGS "auto_mana", "merc_pot", "drop_light", "map_mark", "item_notes", "icloud", "pot_pick", "drop_haptic", "gem_pick", "store_btn", "loot_filter", "auto_buff", "touch_swap_above", "free_tp"
#define DM_TF_SETTING_LEVELS "fps_hud", "eco_fps", "q3d", "render_res"
#define DM_TF_S_FPS_SELECTED "fps_selected"
#define DM_TF_S_FPS_EFFECTIVE "fps_effective"
#define DM_TF_S_UPSCALE_SELECTED "upscale_selected"
#define DM_TF_S_UPSCALE_EFFECTIVE "upscale_effective"
#define DM_TF_S_RT_SHADOW_SET "rt_shadow_set"
#define DM_TF_S_RT_SHADOW_EFF "rt_shadow_eff"
#define DM_TF_S_RT_AO "rt_ao"
#define DM_TF_S_FRAMEGEN "framegen"
#define DM_TF_S_INPUT_MODE "input_mode"
#define DM_TF_S_PAD_TYPE "pad_type"
#define DM_TF_S_LANG "lang"
#define DM_TF_S_FONT "font"
#define DM_TF_S_DIFFICULTY "difficulty_setting"
// q3d 0–4 → 超分档名：与玩家选的档一一对应（PRD 的 performance 档我们没有，已列进给服务端的问题） | q3d 0–4 → profile names, one to one with what the player picks (the PRD's "performance" does not exist here; raised with the server side)
#define DM_TF_UPSCALE_NAMES { "auto", "eco", "balanced", "quality", "off" }
