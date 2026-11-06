#ifndef RECAP_GAME_COOLDOWN_MATH_H
#define RECAP_GAME_COOLDOWN_MATH_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace Game::CooldownMath {
    inline std::uint32_t RemainingMilliseconds(std::uint64_t end, std::uint64_t now) noexcept {
        if (end <= now) return 0;
        return static_cast<std::uint32_t>(std::min<std::uint64_t>(
            end - now, std::numeric_limits<std::uint32_t>::max()));
    }

    inline std::uint32_t ExtendRemainingMilliseconds(
        std::uint64_t end, std::uint64_t now, std::uint32_t addition) noexcept {
        const auto remaining = RemainingMilliseconds(end, now);
        const auto max = std::numeric_limits<std::uint32_t>::max();
        return (addition > max - remaining) ? max : remaining + addition;
    }

    inline std::uint32_t ReduceRemainingMilliseconds(
        std::uint64_t end, std::uint64_t now, std::uint32_t reduction) noexcept {
        const auto remaining = RemainingMilliseconds(end, now);
        return (reduction >= remaining) ? 0 : remaining - reduction;
    }

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
