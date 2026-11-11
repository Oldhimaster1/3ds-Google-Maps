#include "street_fly_logic.h"
#include <assert.h>
int main(void) {
    assert(street_fly_clamp_ms(-1) == 0);
    assert(street_fly_clamp_ms(0) == 0);
    assert(street_fly_clamp_ms(749) == 700);
    assert(street_fly_clamp_ms(750) == 800);
    assert(street_fly_clamp_ms(1900) == 1600);
    assert(street_fly_progress(100, 101, 700) == 0.0f);
    assert(street_fly_progress(1200, 100, 700) == 1.0f);
    assert(street_fly_progress(450, 100, 700) == 0.5f);
    assert(street_fly_progress(100, 100, 0) == 1.0f);
    assert(street_fly_scale(0.0f) == 1.0f);
    assert(street_fly_scale(1.0f) == 4.0f);
    assert(street_fly_scale(0.5f) == 2.5f);
    return 0;
}
