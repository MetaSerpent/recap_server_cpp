#include "../darkspore_server/source/game/ai_gambit_selection.h"

#include <cassert>
#include <cstddef>
#include <limits>

namespace AI = Game::AIGambitSelection;

static_assert(AI::NormalizeIndex(0, 0) == 0);
static_assert(AI::NormalizeIndex(4, 0) == 0);
static_assert(AI::NormalizeIndex(2, 3) == 2);
static_assert(AI::NormalizeIndex(3, 3) == 0);
static_assert(AI::NormalizeIndex(11, 3) == 2);
static_assert(AI::AdvanceIndex(0, 0) == 0);
static_assert(AI::AdvanceIndex(0, 1) == 0);
static_assert(AI::AdvanceIndex(0, 3) == 1);
static_assert(AI::AdvanceIndex(1, 3) == 2);
static_assert(AI::AdvanceIndex(2, 3) == 0);
static_assert(AI::AdvanceIndex(5, 3) == 0);
static_assert(AI::AdvanceIndex(std::numeric_limits<std::size_t>::max(), 1) == 0);

int main() {
    for (std::size_t count = 1; count < 100; ++count) {
        for (std::size_t index = 0; index < 200; ++index) {
            assert(AI::NormalizeIndex(index, count) < count);
            assert(AI::AdvanceIndex(index, count) < count);
        }
    }
}
