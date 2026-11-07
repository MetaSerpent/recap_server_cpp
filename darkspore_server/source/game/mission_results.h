#ifndef RECAP_GAME_MISSION_RESULTS_H
#define RECAP_GAME_MISSION_RESULTS_H

#include <cstddef>
#include <cstdint>
#include <limits>

namespace Game::MissionResults {
    // The wire format reserves one uint32_t for four 8-bit medal values,
    // even though a level may contain more than four objectives.
    constexpr std::uint32_t AddPackedMedal(std::uint32_t packed,
                                           std::size_t index,
                                           std::uint8_t medal) noexcept {
        return index < sizeof(packed) ?
            packed | (std::uint32_t{medal} << (8U * index)) : packed;
    }

    // Adding rewards cannot wrap the persistent account's uint32_t DNA.
    constexpr std::uint32_t AddDna(std::uint32_t current,
                                   std::uint32_t reward) noexcept {
        const auto capacity = std::numeric_limits<std::uint32_t>::max() - current;
        return current + (reward > capacity ? capacity : reward);
    }
}

#endif
