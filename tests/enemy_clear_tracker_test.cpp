#include "../darkspore_server/source/game/enemy_clear_tracker.h"
#include <cassert>

int main() {
    Game::EnemyClearTracker tracker;
    assert(tracker.GetKillPercent() == 0);
    assert(tracker.GetMedal() == 1);
    assert(!tracker.RegisterSpawn(0));
    assert(!tracker.RecordDeath(42));

    for (std::uint32_t id = 1; id <= 100; ++id) {
        assert(tracker.RegisterSpawn(id));
        assert(!tracker.RegisterSpawn(id));
    }
    assert(tracker.SpawnCount() == 100);
    for (std::uint32_t id = 1; id <= 49; ++id) {
        assert(tracker.RecordDeath(id));
        assert(!tracker.RecordDeath(id));
    }
    assert(tracker.GetKillPercent() == 49);
    assert(tracker.GetMedal() == 1);
    assert(tracker.RecordDeath(50));
    assert(tracker.GetMedal() == 2);
    for (std::uint32_t id = 51; id <= 75; ++id) assert(tracker.RecordDeath(id));
    assert(tracker.GetMedal() == 3);
    for (std::uint32_t id = 76; id <= 95; ++id) assert(tracker.RecordDeath(id));
    assert(tracker.GetMedal() == 4);
    assert(tracker.GetKillPercent() == 95);
    assert(!tracker.RecordDeath(101));
    assert(tracker.DefeatedCount() == 95);

    // Late spawns increase the denominator; repeated create packets do not.
    assert(tracker.RegisterSpawn(101));
    assert(tracker.SpawnCount() == 101);
    assert(tracker.GetMedal() == 3);
    assert(tracker.RecordDeath(101));
    assert(tracker.GetMedal() == 4);

    tracker.Reset();
    assert(tracker.SpawnCount() == 0);
    assert(tracker.DefeatedCount() == 0);
    assert(tracker.GetKillPercent() == 0);
    assert(tracker.GetMedal() == 1);
}
