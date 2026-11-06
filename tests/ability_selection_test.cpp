#include "../darkspore_server/source/game/ability_selection.h"

#include <cassert>
#include <cstdint>
#include <initializer_list>

using namespace Game::AbilitySelection;

static_assert(IsValidAbilityIndex(0));
static_assert(IsValidAbilityIndex(8));
static_assert(!IsValidAbilityIndex(9));
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
