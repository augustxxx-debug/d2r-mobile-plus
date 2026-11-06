// iPhone Duo 内外屏切换时的界面方向（B115）。纯判定放这里（tests/t_foldland.c 能在没真机时跑），UIKit 那一半在 dm_foldland_ios.m
// Interface orientation across an iPhone Duo inner / cover screen switch (B115). The pure decisions live here (tests/t_foldland.c runs them without a device); the UIKit half is in dm_foldland_ios.m
#ifndef DM_FOLDLAND_H
#define DM_FOLDLAND_H
// 数值与 UIInterfaceOrientation / UIDeviceOrientation 一致 | Values match UIInterfaceOrientation / UIDeviceOrientation
enum { DM_FL_IO_RIGHT = 3, DM_FL_IO_LEFT = 4, DM_FL_DEV_LEFT = 3, DM_FL_DEV_RIGHT = 4 };
enum { DM_FL_NONE = 0, DM_FL_CLOSING = 1, DM_FL_OPENING = 2 };
int dm_foldland_screen(int *last_short, int native_short);   // 记本屏原生短边，返回这次是合上 / 翻开 / 没切 | Records this screen's native short side; returns closing / opening / no switch
int dm_foldland_target(int kind, int cur_io, int dev);   // 该落到哪个界面方向，0 = 不动 | Which interface orientation to land on, 0 = leave it
#endif
