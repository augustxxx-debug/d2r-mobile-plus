// Fixed viewport for input unit tests. Adaptive geometry is tested by t_uiscreen_frontend.
#include <stdint.h>
#include <string.h>
int dm_ui_native_frontend(void) { return 0; }
int dm_ui_class_x(int x) { return x; }
int dm_ui_bottom_offset(int margin) { (void)margin; return 0; }
// The fixture preserves coordinates without imposing a production packing format.
uint64_t dm_ui_point_pack(float x, float y) { uint32_t a,b; memcpy(&a,&x,4); memcpy(&b,&y,4); return ((uint64_t)a<<32)|b; }
float dm_ui_point_x(uint64_t p) { uint32_t a=p>>32; float x; memcpy(&x,&a,4); return x; }
float dm_ui_point_y(uint64_t p) { uint32_t a=p; float y; memcpy(&y,&a,4); return y; }
