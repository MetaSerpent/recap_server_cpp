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

    // Creature template ability slots used by Player::GetAbilityId:
    //   0 basic, 1 special_1 (hero's own), 2 special_2 (squad), 3 random, 4 passive.
    // The installed PlayerClass data lists, in parsed order: 0 a numeric id that
    // names no ability script, 1 "<Hero>Basic", 2 "<Hero>Active",
    // 3 "<Hero>Random"/variant, 4 "<Hero>Support" (e.g. PlasmaSentinelBasic,
    // PlasmaSentinelActive, PlasmaRandom_FireWave, PlasmaSentinelSupport).
    // Mapping by those names is provisional until verified with client casts.
    inline constexpr int NoPlayerClassField = -1;

    constexpr int PlayerClassFieldForTemplateSlot(std::uint32_t templateSlot) noexcept {
        switch (templateSlot) {
            case 0: return 1;
            case 1: return 2;
            case 2: return 4;
            case 3: return 3;
            default: return NoPlayerClassField;
        }
    }
}

#endif
