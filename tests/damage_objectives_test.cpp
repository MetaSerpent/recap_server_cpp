#include "../darkspore_server/source/game/damage_objectives.h"
#include <cassert>
#include <cstdint>
#include <limits>

using Game::DamageObjectives;
static_assert(DamageObjectives::DifficultyThreshold(150, 500, 0) == 150);
static_assert(DamageObjectives::DifficultyThreshold(150, 500, 7) == 500);
static_assert(DamageObjectives::DifficultyThreshold(150, 500, 99) == 500);
static_assert(DamageObjectives::DifficultyThreshold(100, 350, 7) == 350);
static_assert(DamageObjectives::DifficultyThreshold(50, 200, 7) == 200);

int main() {
    DamageObjectives scores;
    assert(scores.ScoreDamageOften(0, 0) == 1);
    assert(scores.ScoreHugeDamage(0) == 1);
    assert(!scores.RecordHit(0, 0.f));
    assert(!scores.RecordHit(0, -1.f));
    assert(!scores.RecordHit(0, std::numeric_limits<float>::quiet_NaN()));
    assert(!scores.RecordHit(0, std::numeric_limits<float>::infinity()));
    assert(!scores.RecordHit(0, 50.f, 0.f));

    for (int i = 0; i < 49; ++i) assert(scores.RecordHit(0, 35.f));
    assert(scores.GetHitCount(0) == 49);
    assert(scores.GetHugeHitCount(0) == 0);
    assert(scores.ScoreDamageOften(0, 0) == 1);
    assert(scores.RecordHit(0, 35.01f));
    assert(scores.GetHugeHitCount(0) == 1);
    assert(scores.ScoreDamageOften(0, 0) == 2);
    for (int i = 0; i < 4; ++i) scores.RecordHit(0, 36.f);
    assert(scores.ScoreHugeDamage(0) == 2);
    for (int i = 0; i < 10; ++i) scores.RecordHit(0, 36.f);
    assert(scores.ScoreHugeDamage(0) == 3);
    for (int i = 0; i < 15; ++i) scores.RecordHit(0, 36.f);
    assert(scores.ScoreHugeDamage(0) == 4);

    for (int i = 0; i < 71; ++i) scores.RecordHit(0, 1.f);
    assert(scores.ScoreDamageOften(0, 0) == 3);
    for (int i = 0; i < 50; ++i) scores.RecordHit(0, 1.f);
    assert(scores.ScoreDamageOften(0, 0) == 4);
    assert(scores.ScoreDamageOften(0, 7) == 1);
    assert(scores.GetHitCount(1) == 0);
    assert(scores.RecordHit(1, 100.f, 3.f));
    assert(scores.GetHugeHitCount(1) == 0);
    assert(scores.RecordHit(1, 110.f, 3.f));
    assert(scores.GetHugeHitCount(1) == 1);
    assert(scores.GetHugeHitCount(0) == 30);
    scores.Reset();
    assert(scores.GetHitCount(0) == 0);
    assert(scores.GetHugeHitCount(0) == 0);
}
