#ifndef RECAP_GAME_DAMAGE_OBJECTIVES_H
#define RECAP_GAME_DAMAGE_OBJECTIVES_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace Game {
    // Original objective_DoDamageOften.lua uses difficulty-scaled thresholds
    // with a maximum major-difficulty scale of seven.
    // objective_HugeDamage.lua requires damage > 35 * enemy health multiplier;
    // the latter is supplied explicitly until difficulty scaling is restored.
    class DamageObjectives {
    public:
        struct PlayerCounters {
            std::uint32_t hits = 0;
            std::uint32_t hugeHits = 0;
        };

        void Reset() { mPlayers.clear(); }

        bool RecordHit(std::uint8_t playerId, float appliedDamage,
                       float healthMultiplier = 1.f) {
            if (!std::isfinite(appliedDamage) || appliedDamage <= 0.f ||
                !std::isfinite(healthMultiplier) || healthMultiplier <= 0.f) {
                return false;
            }
            auto& player = mPlayers[playerId];
            if (player.hits < UINT32_MAX) {
                ++player.hits;
            }
            if (appliedDamage > (35.f * healthMultiplier) &&
                player.hugeHits < UINT32_MAX) {
                ++player.hugeHits;
            }
            return true;
        }

        std::uint32_t GetHitCount(std::uint8_t id) const {
            return GetCounters(id).hits;
        }

        std::uint32_t GetHugeHitCount(std::uint8_t id) const {
            return GetCounters(id).hugeHits;
        }

        static std::uint32_t DifficultyThreshold(std::uint32_t minimum,
                                                  std::uint32_t maximum,
                                                  std::uint32_t majorDifficulty) {
            const auto difficulty = std::min(majorDifficulty, 7U);
            return minimum + ((maximum - minimum) * difficulty) / 7U;
        }

        // RakNet::ObjectiveMedal: Failed=1, Bronze=2, Silver=3, Gold=4.
        std::uint8_t ScoreDamageOften(std::uint8_t id,
                                      std::uint32_t majorDifficulty) const {
            const auto hits = GetHitCount(id);
            if (hits >= DifficultyThreshold(150, 500, majorDifficulty)) return 4;
            if (hits >= DifficultyThreshold(100, 350, majorDifficulty)) return 3;
            if (hits >= DifficultyThreshold(50, 200, majorDifficulty)) return 2;
            return 1;
        }

        std::uint8_t ScoreHugeDamage(std::uint8_t id) const {
            const auto hits = GetHugeHitCount(id);
            if (hits >= 30) return 4;
            if (hits >= 15) return 3;
            if (hits >= 5) return 2;
            return 1;
        }

    private:
        const PlayerCounters& GetCounters(std::uint8_t id) const {
            const auto it = mPlayers.find(id);
            return it == mPlayers.end() ? mEmpty : it->second;
        }

        std::unordered_map<std::uint8_t, PlayerCounters> mPlayers;
        const PlayerCounters mEmpty{};
    };
}

#endif
