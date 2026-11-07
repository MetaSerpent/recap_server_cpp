#ifndef RECAP_GAME_CASHOUT_REWARDS_H
#define RECAP_GAME_CASHOUT_REWARDS_H

#include "mission_results.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace Game::MissionResults {
    constexpr std::size_t CashOutRewardCount(std::uint32_t planetsCompleted) noexcept {
        return std::min<std::uint32_t>(planetsCompleted, 4U);
    }

    // Source: native chain cash-out reward-level calculation recovered by
    // darkspinnet/darkspin, server/zone/result/reward.go.
    constexpr std::uint32_t CashOutRewardLevel(std::uint32_t difficulty,
                                               std::uint32_t planetsCompleted) noexcept {
        if (difficulty == 0 || planetsCompleted == 0) return 0;
        const auto major = (difficulty - 1) / 4 + 1;
        const auto minor = (difficulty - 1) % 4 + 1;
        const auto level = 10U * major + minor;
        if (minor == 4) return level + 6;
        return level + std::min(planetsCompleted - 1, 3U);
    }

    constexpr std::uint64_t CashOutMix(std::uint64_t value) noexcept {
        value ^= value >> 30;
        value *= UINT64_C(0xbf58476d1ce4e5b9);
        value ^= value >> 27;
        value *= UINT64_C(0x94d049bb133111eb);
        value ^= value >> 31;
        return value;
    }

    constexpr std::uint64_t MakeCashOutReceiptId(std::uint64_t nonce,
                                                  std::uint64_t accountId) noexcept {
        if (nonce == 0 || accountId == 0) return 0;
        const auto mixed = CashOutMix(nonce ^ (accountId * UINT64_C(0x9e3779b97f4a7c15)));
        return mixed == 0 ? UINT64_C(1) : mixed;
    }

    // One-based stable client roll, same for receipt resends.
    constexpr std::uint32_t CashOutRewardRoll(std::uint64_t receiptId,
                                               std::size_t rewardIndex) noexcept {
        if (receiptId == 0 || rewardIndex >= 4) return 0;
        const auto draw = receiptId +
            (static_cast<std::uint64_t>(rewardIndex) + 1) * UINT64_C(0x9e3779b97f4a7c15);
        return static_cast<std::uint32_t>(CashOutMix(draw) % 100) + 1;
    }

    // The fork's SporeNet::PartRarity uses Unique/RareUnique/EpicUnique
    // (4/5/6). The class/element-specific generator is still pending.
    constexpr std::uint8_t CashOutRewardRarity(
        std::uint32_t roll, RarityBoundaries bands) noexcept {
        if (roll == 0 || roll > 100 || bands.upper > 100 ||
            bands.lower > bands.upper) return 0;
        if (roll <= 100 - bands.upper) return 4; // Special
        if (roll <= 100 - bands.lower) return 5; // Rarified
        return 6; // Purified
    }

    // Temporary deterministic selection from the 1..1573 rigblock IDs
    // accepted by SporeNet::Part::SetRigblock. No client packet claims a
    // separately generated item without the matching inventory save.
    constexpr std::uint16_t CashOutRigblock(std::uint64_t receiptId,
                                            std::size_t rewardIndex) noexcept {
        if (receiptId == 0 || rewardIndex >= 4) return 0;
        const auto draw = CashOutMix(
            receiptId ^ ((static_cast<std::uint64_t>(rewardIndex) + 1) *
                         UINT64_C(0xd1b54a32d192ed03)));
        return static_cast<std::uint16_t>(draw % 1573 + 1);
    }
}

#endif
