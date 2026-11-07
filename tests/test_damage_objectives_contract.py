"""Combat objective accounting must originate from real Lua damage results."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "darkspore_server"
INSTANCE = (ROOT / "source/game/instance.cpp").read_text(encoding="utf-8")
LUA_SRC = (ROOT / "source/game/lua.cpp").read_text(encoding="utf-8")
TRACKER = (ROOT / "source/game/damage_objectives.h").read_text(encoding="utf-8")


class DamageObjectiveContracts(unittest.TestCase):
    def test_source_uses_post_hit_health_delta(self):
        snippet = LUA_SRC.split("auto TakeDamage(sol::this_state L,", 1)[1].split(
            "auto Heal(sol::this_state L,", 1
        )[0]
        self.assertIn("const float healthBeforeHit = object->GetHealth();", snippet)
        self.assertIn("std::max(0.f, healthBeforeHit - object->GetHealth())", snippet)
        self.assertIn("game.OnDamageDealt(attacker, object,", snippet)

    def test_scoring_requires_player_and_npc(self):
        snippet = INSTANCE.split("void Instance::OnDamageDealt(", 1)[1].split(
            "void Instance::OnObjectDeath(", 1
        )[0]
        self.assertIn("!attacker->IsPlayerControlled()", snippet)
        self.assertIn("noun->GetNonPlayerClassData()", snippet)
        self.assertIn("attacker->GetTeam() == victim->GetTeam()", snippet)
        self.assertIn("mDamageObjectives.RecordHit(playerId, appliedDamage, 1.0f)", snippet)

    def test_player_specific_medals_are_computed_before_completion(self):
        snippet = INSTANCE.split("void Instance::BeamOut(", 1)[1].split(
            "bool Instance::SendObjectCreate(", 1
        )[0]
        self.assertIn('mDamageObjectives.ScoreDamageOften(player->GetId(), major)', snippet)
        self.assertIn("mDamageObjectives.ScoreHugeDamage(player->GetId())", snippet)
        self.assertLess(snippet.index("mDamageObjectives.ScoreHugeDamage("),
                        snippet.index("SendObjectivesComplete(client);"))
        self.assertNotIn("mCashOutData.mGoldMedals.fill(3);", snippet)

    def test_lua_thresholds_are_explicit(self):
        for script, lines in [
            ("objective_DoDamageOften.lua", ["goldDamageCountMin = 150", "goldDamageCountMax = 500",
                                              "silverDamageCountMin = 100", "silverDamageCountMax = 350",
                                              "bronzeDamageCountMin = 50", "bronzeDamageCountMax = 200",
                                              "difficultyScale = 7"]),
            ("objective_HugeDamage.lua", ["goldDamageCount = 30", "silverDamageCount = 15",
                                           "bronzeDamageCount = 5", "damageThresholdMultiplayer = 35"]),
        ]:
            text = (ROOT / "res/data/serverdata/lua" / script).read_text(encoding="utf-8")
            for line in lines:
                self.assertIn(line, text)
        self.assertIn("DifficultyThreshold(150, 500", TRACKER)
        self.assertIn("appliedDamage > (35.f * healthMultiplier)", TRACKER)


if __name__ == "__main__":
    unittest.main()
