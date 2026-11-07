#ifndef RECAP_GAME_ENEMY_CLEAR_TRACKER_H
#define RECAP_GAME_ENEMY_CLEAR_TRACKER_H

#include <cstdint>
#include <unordered_set>

namespace Game {
    // Tracks unique spawned non-player enemies and their first deaths.
    // Score thresholds come from bundled objective_DefeatAllMonsters.lua.
    class EnemyClearTracker {
    public:
        void Reset() {
            mSpawns.clear();
            mDefeated.clear();
        }

        bool RegisterSpawn(std::uint32_t id) {
            return id != 0 && mSpawns.insert(id).second;
        }

        bool RecordDeath(std::uint32_t id) {
            return mSpawns.contains(id) && mDefeated.insert(id).second;
        }

        std::uint32_t GetKillPercent() const {
            if (mSpawns.empty()) {
                return 0;
            }
            return static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(mDefeated.size()) * 100) / mSpawns.size());
        }

        // RakNet::ObjectiveMedal: Failed=1, Bronze=2, Silver=3, Gold=4.
        std::uint8_t GetMedal() const {
            if (mSpawns.empty()) {
                return 1;
            }
            const auto killed = static_cast<std::uint64_t>(mDefeated.size());
            const auto spawned = static_cast<std::uint64_t>(mSpawns.size());
            if (killed * 100 >= spawned * 95) return 4;
            if (killed * 100 >= spawned * 75) return 3;
            if (killed * 100 >= spawned * 50) return 2;
            return 1;
        }

        std::size_t SpawnCount() const { return mSpawns.size(); }
        std::size_t DefeatedCount() const { return mDefeated.size(); }

    private:
        std::unordered_set<std::uint32_t> mSpawns;
        std::unordered_set<std::uint32_t> mDefeated;
    };
}

#endif
