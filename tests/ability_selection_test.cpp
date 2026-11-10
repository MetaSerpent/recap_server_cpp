#include "../darkspore_server/source/game/ability_selection.h"

#include <cassert>
#include <cstdint>
#include <initializer_list>

using namespace Game::AbilitySelection;

static_assert(IsValidAbilityIndex(0));
static_assert(IsValidAbilityIndex(8));
static_assert(!IsValidAbilityIndex(9));

// Template slot -> PlayerClass field: basic -> "<Hero>Basic", special_1 ->
// "<Hero>Active", special_2 (squad) -> "<Hero>Support", random -> variant.
// Field 0 (a numeric id naming no script) and the passive slot are not used.
static_assert(PlayerClassFieldForTemplateSlot(0) == 1);
static_assert(PlayerClassFieldForTemplateSlot(1) == 2);
static_assert(PlayerClassFieldForTemplateSlot(2) == 4);
static_assert(PlayerClassFieldForTemplateSlot(3) == 3);
static_assert(PlayerClassFieldForTemplateSlot(4) == NoPlayerClassField);
static_assert(!IsValidAbilityIndex(256));
static_assert(IsValidHeroSlot(0, 3));
static_assert(IsValidHeroSlot(2, 3));
static_assert(!IsValidHeroSlot(3, 3));
static_assert(!IsValidHeroSlot(2, 2));
static_assert(!IsValidHeroSlot(0, 0));
static_assert(!IsSquadAbility(3));
static_assert(IsSquadAbility(6));
static_assert(IsSquadAbility(7));
static_assert(IsSquadAbility(8));
static_assert(!IsSquadAbility(9));

int main() {
    for (std::uint32_t active = 0; active < MaxHeroes; ++active) {
        for (std::uint32_t ability : {0u, 2u, 3u}) {
            assert(ResolveHeroForAbility(active, ability) == active);
        }
        for (std::uint32_t ability = 6; ability <= 8; ++ability) {
            assert(ResolveHeroForAbility(active, ability) == ability - 6);
        }
    }
    return 0;
}
