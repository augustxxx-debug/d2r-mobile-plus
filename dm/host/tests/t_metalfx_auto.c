#include <assert.h>
#include <stdio.h>
#include "../../gfx/nvn/nvn_metalfx_policy.h"
#define SEC 1000000000ull
int main(void) {
    nvn_metalfx_auto_state s = {0};
    assert(nvn_metalfx_auto_step(&s,2000000,0,0,0)==.80f);
    assert(nvn_metalfx_auto_step(&s,2000000,2,0,SEC)==.65f);
    for (unsigned t=2;t<302;t++)
        assert(nvn_metalfx_auto_step(&s,2000000,t%2?2:1,0,t*SEC)==.65f);
    for (unsigned t=302;t<362;t++)
        assert(nvn_metalfx_auto_step(&s,2000000,1,0,t*SEC)==.65f);
    assert(nvn_metalfx_auto_step(&s,2000000,1,0,362*SEC)==.80f);
    assert(nvn_metalfx_auto_step(&s,2000000,0,1,363*SEC)==.65f);
    for (unsigned t=364;t<400;t++)
        assert(nvn_metalfx_auto_step(&s,2000000,0,0,t*SEC)==.65f);
    assert(nvn_metalfx_auto_step(&s,2000000,0,0,500*SEC)==.65f);
    for (unsigned t=501;t<560;t++)
        assert(nvn_metalfx_auto_step(&s,2000000,0,0,t*SEC)==.65f);
    assert(nvn_metalfx_auto_step(&s,2000000,0,0,560*SEC)==.80f);
    assert(nvn_metalfx_auto_step(&s,5700000,0,0,561*SEC)==.70f);
    for (unsigned t=562;t<862;t++)
        assert(nvn_metalfx_auto_step(&s,t%2?5700000:2000000,0,0,t*SEC)==.70f);
    assert(nvn_metalfx_auto_step(&s,2000000,0,0,SEC)==.70f);
    assert(nvn_metalfx_scale(1,0,3,1)==.60f);
    assert(nvn_metalfx_scale(2,0,3,1)==.75f);
    assert(nvn_metalfx_scale(3,0,3,1)==.85f);
    puts("PASS MetalFX Auto: thermal chatter, low-power recovery, long gaps, screen-size chatter, clock rollback and manual profiles");
}
