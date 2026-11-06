#include "../darkspore_server/source/game/cooldown_math.h"
#include <cassert>
#include <cstdint>
#include <limits>

int main() {
    using Game::CooldownMath::ScaleRemainingMilliseconds;
    assert(ScaleRemainingMilliseconds(1000, 500, 1.f) == 500);
    assert(ScaleRemainingMilliseconds(1000, 500, 0.5f) == 250);
    assert(ScaleRemainingMilliseconds(1000, 500, 2.f) == 1000);
    assert(ScaleRemainingMilliseconds(1000, 500, 0.f) == 0);
    assert(ScaleRemainingMilliseconds(500, 500, 1.f) == 0);
    assert(ScaleRemainingMilliseconds(400, 500, 1.f) == 0);
    assert(ScaleRemainingMilliseconds(1000, 500, -1.f) == 0);
    assert(ScaleRemainingMilliseconds(1000, 500, std::numeric_limits<float>::infinity()) == 0);
    assert(ScaleRemainingMilliseconds(1000, 500, std::numeric_limits<float>::quiet_NaN()) == 0);
    assert(ScaleRemainingMilliseconds(5000000000ULL, 0, 10.f)
           == std::numeric_limits<std::uint32_t>::max());
    return 0;
}
