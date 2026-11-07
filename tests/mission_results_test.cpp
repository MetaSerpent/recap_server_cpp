#include "../darkspore_server/source/game/mission_results.h"
#include <cassert>
#include <cstdint>

using Game::MissionResults::AddPackedMedal;

static_assert(AddPackedMedal(0u, 0, 4) == 0x00000004u);
static_assert(AddPackedMedal(0x00000004u, 1, 3) == 0x00000304u);
static_assert(AddPackedMedal(0x00000304u, 2, 2) == 0x00020304u);
static_assert(AddPackedMedal(0x00020304u, 3, 1) == 0x01020304u);
static_assert(AddPackedMedal(0x01020304u, 4, 0xFF) == 0x01020304u);
static_assert(AddPackedMedal(0x01020304u, 255, 0xFF) == 0x01020304u);

int main() {
    uint32_t packed = 0;
    for (std::size_t i = 0; i < 5; ++i) {
        packed = AddPackedMedal(packed, i, static_cast<uint8_t>(i + 1));
    }
    assert(packed == 0x04030201u);
}
