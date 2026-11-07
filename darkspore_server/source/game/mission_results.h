#ifndef RECAP_GAME_MISSION_RESULTS_H
#define RECAP_GAME_MISSION_RESULTS_H

#include <cstddef>
#include <cstdint>

namespace Game::MissionResults {
    // The wire format reserves one uint32_t for four 8-bit medal values,
    // even though a level may contain more than four objectives.
    constexpr std::uint32_t AddPackedMedal(std::uint32_t packed,
                                           std::size_t index,
                                           std::uint8_t medal) noexcept {
        return index < sizeof(packed) ?
            packed | (std::uint32_t{medal} << (8U * index)) : packed;
    }
}

#endif
