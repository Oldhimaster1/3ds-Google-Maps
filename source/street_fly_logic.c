#include "street_fly_logic.h"
int street_fly_clamp_ms(int value) {
    if (value <= 0) return 0;
    if (value >= STREET_FLY_MAX_MS) return STREET_FLY_MAX_MS;
    return ((value + 50) / 100) * 100;
}
float street_fly_progress(uint64_t now, uint64_t start, int duration_ms) {
    if (duration_ms <= 0) return 1.0f;
    if (now <= start) return 0.0f;
    uint64_t elapsed = now - start;
    if (elapsed >= (uint64_t)duration_ms) return 1.0f;
    return (float)elapsed / (float)duration_ms;
}
float street_fly_scale(float progress) {
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    float eased = progress * progress * (3.0f - 2.0f * progress);
    return 1.0f + 3.0f * eased;
}
