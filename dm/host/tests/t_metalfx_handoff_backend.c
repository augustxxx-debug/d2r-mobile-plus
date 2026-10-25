// Strong backend symbols override the real host's weak no-MetalFX fallbacks.
extern float test_fx_hint;
extern int test_fx_auto;
float mtl_metalfx_scale_hint(void) { return test_fx_hint; }
int mtl_metalfx_auto_selected(void) { return test_fx_auto; }
