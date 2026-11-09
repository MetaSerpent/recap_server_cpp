#ifndef RECAP_MISSION_PROGRESSION_H
#define RECAP_MISSION_PROGRESSION_H

#include <cstdint>
#include <string_view>

// The client stores one run as a sequence of at most five planets.
// This helper is intentionally independent of RakNet and game assets.
namespace Game::Progression {
    inline constexpr std::uint8_t MaxPlanetsPerChain = 5;
    // Bundled ChainVoteData::levelNames has tutorial plus 24 levels.
    inline constexpr std::uint32_t FinalCampaignLevelIndex = 24;

    // finalLevelIndex is the number of campaign levels: 24 in the built-in
    // table, 72 in the 5.3.0.103 ChainLevels data.
    constexpr bool CanDeployNextPlanet(std::uint8_t completedPlanets,
                                       std::uint32_t levelIndex,
                                       std::uint32_t finalLevelIndex = FinalCampaignLevelIndex) noexcept {
        return completedPlanets > 0 && completedPlanets < MaxPlanetsPerChain &&
               levelIndex < finalLevelIndex;
    }

    constexpr std::uint8_t CompletePlanet(std::uint8_t current) noexcept {
        return current < MaxPlanetsPerChain ? static_cast<std::uint8_t>(current + 1)
                                          : MaxPlanetsPerChain;
    }

    // The tutorial level differs between client builds: 5.3.0.127 ships
    // Darkspore_Tutorial_cryos_1_v2, the 5.3.0.103 Steam demo only
    // Darkspore_Tutorial_cryos_1. The name is hashed into the chain data sent to
    // the client, so it must name a level the installed game actually has.
    inline constexpr std::string_view TutorialLevelV2 = "Darkspore_Tutorial_cryos_1_v2";
    inline constexpr std::string_view TutorialLevelV1 = "Darkspore_Tutorial_cryos_1";

    constexpr std::string_view SelectTutorialLevel(bool hasV2Level, bool hasV1Level) noexcept {
        if (!hasV2Level && hasV1Level) {
            return TutorialLevelV1;
        }
        return TutorialLevelV2;
    }
}
#endif
