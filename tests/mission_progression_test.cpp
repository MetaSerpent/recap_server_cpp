#include "../darkspore_server/source/game/mission_progression.h"
#include <cassert>
#include <cstdint>

using namespace Game::Progression;

static_assert(CompletePlanet(0) == 1);
static_assert(CompletePlanet(1) == 2);
static_assert(CompletePlanet(2) == 3);
static_assert(CompletePlanet(3) == 4);
static_assert(CompletePlanet(4) == 5);
static_assert(CompletePlanet(5) == 5);
static_assert(CompletePlanet(255) == 5);
static_assert(!CanDeployNextPlanet(0, 1));
static_assert(CanDeployNextPlanet(1, 0));
static_assert(CanDeployNextPlanet(4, 23));
static_assert(!CanDeployNextPlanet(5, 23));
static_assert(!CanDeployNextPlanet(1, 24));
static_assert(!CanDeployNextPlanet(255, 0));

int main() {
    for (std::uint8_t i = 0; i < 5; ++i) {
        assert(CompletePlanet(i) == i + 1);
    }
}
