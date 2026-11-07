#include "../darkspore_server/source/game/objective_scoring.h"
#include <cassert>
#include <cstdint>
#include <limits>

using Game::ObjectiveScoring::ScoreFinishLevelQuickly;

static_assert(ScoreFinishLevelQuickly(0) == 4);
static_assert(ScoreFinishLevelQuickly(599) == 4);
static_assert(ScoreFinishLevelQuickly(600) == 4);
static_assert(ScoreFinishLevelQuickly(601) == 3);
static_assert(ScoreFinishLevelQuickly(900) == 3);
static_assert(ScoreFinishLevelQuickly(901) == 2);
static_assert(ScoreFinishLevelQuickly(1200) == 2);
static_assert(ScoreFinishLevelQuickly(1201) == 1);
static_assert(ScoreFinishLevelQuickly(std::numeric_limits<uint64_t>::max()) == 1);

int main() {
    for (std::uint64_t i = 0; i <= 2000; ++i) {
        const auto grade = ScoreFinishLevelQuickly(i);
        assert(grade >= 1 && grade <= 4);
        if (i > 0) {
            assert(grade <= ScoreFinishLevelQuickly(i - 1));
        }
    }
}
