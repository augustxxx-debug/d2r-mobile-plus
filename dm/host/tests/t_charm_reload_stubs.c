// t_charm_reload 的链接桩：dm_bag.c 其余部分用到、这个用例走不到的外部符号 | link stubs for externals the test never reaches
void dm_cursor_item(void) {} void dm_cursor_item_size(void) {} void dm_game_notice_ui(void) {} void dm_guest_frame(void) {} void dm_hud_get(void) {} void dm_hud_held(void) {}
void dm_padmode_current(void) {} void dm_padmode_page_keys(void) {} void dm_padmode_raw_buttons(void) {} void dm_str(void) {} void dm_ui_hover_now(void) {} void dm_ui_modal_active(void) {} void dm_custom_get(void) {} void dm_padmode_raw_dirs(void) {}
void dm_ui_top_chk(void) {} void dm_ui_top_chk_on(void) {}
unsigned long long dm_fs_commit_count(void) { return 0; }   // D1 护身符页退局等提交（B203）合进来后 dm_bag_srv_tick 用到 | used by dm_bag_srv_tick once the D1 charm-page exit wait (B203) is merged
void dm_held_equip(int bodyloc) { (void)bodyloc; }   // B222 腰带盖住鞋格时按 A 直接放栏位，dm_bag_ui 用到 | used by dm_bag_ui once B222 (A on a slot covered by the belt equips directly) is merged
