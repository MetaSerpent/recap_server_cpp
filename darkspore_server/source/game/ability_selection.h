#ifndef RECAP_GAME_ABILITY_SELECTION_H
#define RECAP_GAME_ABILITY_SELECTION_H

#include <cstddef>
#include <cstdint>

// Pure helpers shared by Player and RakNet command handling.
namespace Game::AbilitySelection {
    inline constexpr std::size_t MaxHeroes = 3;
    inline constexpr std::uint32_t MaxAbilityIndex = 8;

    constexpr bool IsValidAbilityIndex(std::uint32_t index) noexcept {
        return index <= MaxAbilityIndex;
    }

    constexpr bool IsValidHeroSlot(std::uint32_t slot, std::size_t available) noexcept {
        return slot < MaxHeroes && slot < available;
    }

    constexpr bool IsSquadAbility(std::uint32_t index) noexcept {
        return index >= 6 && index <= MaxAbilityIndex;
    }

    constexpr std::uint32_t ResolveHeroForAbility(std::uint32_t currentHero, std::uint32_t abilityIndex) noexcept {
        return IsSquadAbility(abilityIndex) ? abilityIndex - 6 : currentHero;
    }
}

#endif
