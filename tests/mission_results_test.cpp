#include "../darkspore_server/source/game/mission_results.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <limits>

using Game::MissionResults::AddPackedMedal;
using Game::MissionResults::AddDna;
using Game::MissionResults::CountMedals;
using Game::MissionResults::DecodeDnaPickup;
constexpr std::array<std::uint8_t, 8> objectiveMedals{0, 1, 2, 3, 4, 4, 255, 2};
constexpr auto medalTotals = CountMedals(objectiveMedals, [](auto medal) { return medal; });
static_assert(medalTotals.gold == 2);
static_assert(medalTotals.silver == 1);
static_assert(medalTotals.bronze == 2);

static_assert(AddPackedMedal(0u, 0, 4) == 0x00000004u);
static_assert(AddPackedMedal(0x00000004u, 1, 3) == 0x00000304u);
static_assert(AddPackedMedal(0x00000304u, 2, 2) == 0x00020304u);
static_assert(AddPackedMedal(0x00020304u, 3, 1) == 0x01020304u);
static_assert(AddPackedMedal(0x01020304u, 4, 0xFF) == 0x01020304u);
static_assert(AddPackedMedal(0x01020304u, 255, 0xFF) == 0x01020304u);
static_assert(AddDna(0u, 50u) == 50u);
static_assert(AddDna(300u, 50u) == 350u);
static_assert(AddDna(std::numeric_limits<uint32_t>::max() - 5u, 50u) ==
              std::numeric_limits<uint32_t>::max());
static_assert(AddDna(std::numeric_limits<uint32_t>::max(), 50u) ==
              std::numeric_limits<uint32_t>::max());

int main() {
    assert(!DecodeDnaPickup(0.f));
    assert(!DecodeDnaPickup(-1.f));
    assert(!DecodeDnaPickup(0.9f));
    assert(!DecodeDnaPickup(std::numeric_limits<float>::quiet_NaN()));
    assert(!DecodeDnaPickup(std::numeric_limits<float>::infinity()));
    assert(!DecodeDnaPickup(-std::numeric_limits<float>::infinity()));
    assert(!DecodeDnaPickup(4294967296.f));
    assert(DecodeDnaPickup(1.f).value() == 1);
    assert(DecodeDnaPickup(42.9f).value() == 42);
    assert(DecodeDnaPickup(255.f).value() == 255);
    assert(AddDna(std::numeric_limits<uint32_t>::max() - 2,
                  DecodeDnaPickup(20.f).value()) == std::numeric_limits<uint32_t>::max());

    uint32_t packed = 0;
    for (std::size_t i = 0; i < 5; ++i) {
        packed = AddPackedMedal(packed, i, static_cast<uint8_t>(i + 1));
    }
    assert(packed == 0x04030201u);
}
