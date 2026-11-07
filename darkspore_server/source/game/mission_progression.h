#ifndef RECAP_MISSION_PROGRESSION_H
#define RECAP_MISSION_PROGRESSION_H

#include <cstdint>

// The client stores one run as a sequence of at most five planets.
// This helper is intentionally independent of RakNet and game assets.
namespace Game::Progression {
    inline constexpr std::uint8_t MaxPlanetsPerChain = 5;
    // Bundled ChainVoteData::levelNames has tutorial plus 24 levels.
    inline constexpr std::uint32_t FinalCampaignLevelIndex = 24;

    constexpr bool CanDeployNextPlanet(std::uint8_t completedPlanets,
                                       std::uint32_t levelIndex) noexcept {
        return completedPlanets > 0 && completedPlanets < MaxPlanetsPerChain &&
               levelIndex < FinalCampaignLevelIndex;
    }

    constexpr std::uint8_t CompletePlanet(std::uint8_t current) noexcept {
        return current < MaxPlanetsPerChain ? static_cast<std::uint8_t>(current + 1)
                                          : MaxPlanetsPerChain;
    }
}
#endif
