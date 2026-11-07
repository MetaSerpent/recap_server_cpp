#ifndef RECAP_GAME_OBJECTIVE_SCORING_H
#define RECAP_GAME_OBJECTIVE_SCORING_H

#include <cstdint>

namespace Game::ObjectiveScoring {
    // Matches the ObjectiveStatus values used by the RakNet client:
    // InProgress=0, Failed=1, Bronze=2, Silver=3, Gold=4.
    // The original Objective_FinishLevelQuickly.lua defines 600, 900 and
    // 1200 seconds as the respective medal thresholds.
    constexpr std::uint8_t ScoreFinishLevelQuickly(
        std::uint64_t elapsedSeconds) noexcept {
        return elapsedSeconds <= 600 ? 4 :
               elapsedSeconds <= 900 ? 3 :
               elapsedSeconds <= 1200 ? 2 : 1;
    }

    // Original objective_Obelisks.lua: count distinct accessed obelisks.
    // Gold=3, Silver=2, Bronze=1, Failed=0.
    constexpr std::uint8_t ScoreTouchedObelisks(
        std::uint64_t count) noexcept {
        return count >= 3 ? 4 :
               count >= 2 ? 3 :
               count >= 1 ? 2 : 1;
    }
}

#endif
