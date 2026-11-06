#ifndef RECAP_GAME_COOLDOWN_MATH_H
#define RECAP_GAME_COOLDOWN_MATH_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace Game::CooldownMath {
    // A cooldown multiplier applies to the *remaining* time, starting now.
    // Return a uint32 millisecond duration for the client's wire protocol.
    inline std::uint32_t ScaleRemainingMilliseconds(
        std::uint64_t oldEnd, std::uint64_t now, float multiplier) noexcept {
        if (oldEnd <= now || !(multiplier > 0.0f) || !std::isfinite(multiplier)) {
            return 0;
        }
        const double remaining = static_cast<double>(oldEnd - now);
        const double scaled = std::min(
            remaining * static_cast<double>(multiplier),
            static_cast<double>(std::numeric_limits<std::uint32_t>::max()));
        return static_cast<std::uint32_t>(scaled);
    }
}

#endif
