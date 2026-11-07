#ifndef RECAP_MISSION_PROGRESSION_H
#define RECAP_MISSION_PROGRESSION_H

#include <cstdint>

// The client stores one run as a sequence of at most five planets.
// This helper is intentionally independent of RakNet and game assets.
namespace Game::Progression {
    inline constexpr std::uint8_t MaxPlanetsPerChain = 5;

    constexpr std::uint8_t CompletePlanet(std::uint8_t current) noexcept {
        return current < MaxPlanetsPerChain ? static_cast<std::uint8_t>(current + 1)
                                          : MaxPlanetsPerChain;
    }
}
#endif
