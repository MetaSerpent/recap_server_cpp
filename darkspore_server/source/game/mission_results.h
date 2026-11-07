#ifndef RECAP_GAME_MISSION_RESULTS_H
#define RECAP_GAME_MISSION_RESULTS_H

#include <cstddef>
#include <cstdint>
#include <limits>
#include <cmath>
#include <optional>
#include <utility>

namespace Game::MissionResults {
    // The wire format reserves one uint32_t for four 8-bit medal values,
    // even though a level may contain more than four objectives.
    constexpr std::uint32_t AddPackedMedal(std::uint32_t packed,
                                           std::size_t index,
                                           std::uint8_t medal) noexcept {
        return index < sizeof(packed) ?
            packed | (std::uint32_t{medal} << (8U * index)) : packed;
    }

    struct MedalCounts {
        std::uint32_t gold = 0;
        std::uint32_t silver = 0;
        std::uint32_t bronze = 0;
    };

    // Derive medal totals from a single player's final objective snapshot.
    // A count is not awarded for Failed, InProgress or unknown statuses.
    template<typename Objectives, typename MedalOf>
    constexpr MedalCounts CountMedals(const Objectives& objectives,
                                      MedalOf medalOf) {
        MedalCounts result{};
        for (const auto& objective : objectives) {
            switch (static_cast<std::uint8_t>(medalOf(objective))) {
                case 4: ++result.gold; break;
                case 3: ++result.silver; break;
                case 2: ++result.bronze; break;
                default: break;
            }
        }
        return result;
    }

    // Loot DNA travels as a float in the original wire format. Reject
    // malformed, non-finite, fractional-under-one and overflowing amounts
    // before converting to a uint32_t. Whole-number truncation matches
    // the network's discrete account balance.
    inline std::optional<std::uint32_t> DecodeDnaPickup(float amount) noexcept {
        if (!std::isfinite(amount) || amount < 1.f ||
            static_cast<double>(amount) >= 4294967296.0) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(amount);
    }

    struct RarityBoundaries {
        std::uint32_t upper = 0; // Rarified+Purified threshold
        std::uint32_t lower = 0; // Purified-only threshold
    };

    // Client tooltip weights: bronze=1, silver=3, gold=6.
    // The original level 5-1 (index 17) unlocks a limited Purified band.
    // These are packet presentation boundaries, not item awards.
    constexpr RarityBoundaries CashOutRarityBoundaries(
        std::uint32_t completedIndex, std::uint32_t planetsCompleted,
        MedalCounts medals) noexcept {
        if (planetsCompleted == 0) {
            return {};
        }
        const std::uint64_t weighted = std::uint64_t{medals.bronze} +
            3ULL * medals.silver + 6ULL * medals.gold;
        const auto upper = static_cast<std::uint32_t>(
            weighted / planetsCompleted > 100 ? 100 : weighted / planetsCompleted);
        const auto lower = completedIndex >= 17 ?
            (upper / 5 > 10 ? 10U : upper / 5) : 0U;
        return {upper, lower};
    }

    // Adding rewards cannot wrap the persistent account's uint32_t DNA.
    constexpr std::uint32_t AddDna(std::uint32_t current,
                                   std::uint32_t reward) noexcept {
        const auto capacity = std::numeric_limits<std::uint32_t>::max() - current;
        return current + (reward > capacity ? capacity : reward);
    }
}

#endif
