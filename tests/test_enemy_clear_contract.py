"""Enemy-clear objective uses genuine spawns/deaths and original medal cutoffs."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "darkspore_server"
SOURCE = (ROOT / "source/game/instance.cpp").read_text(encoding="utf-8")
TRACKER = (ROOT / "source/game/enemy_clear_tracker.h").read_text(encoding="utf-8")
LUA = (ROOT / "res/data/serverdata/lua/objective_DefeatAllMonsters.lua").read_text(encoding="utf-8")


class EnemyClearContract(unittest.TestCase):
    def test_medal_thresholds_are_grounded_in_game_scripts(self):
        for key, value in (("goldPercentage", "0.95"), ("silverPercentage", "0.75"),
                           ("bronzePercentage", "0.5")):
            self.assertIn(key + " = " + value, LUA)
        for integer in ("95", "75", "50"):
            self.assertIn("spawned * " + integer, TRACKER)

    def test_world_create_tracks_npcs_without_counting_heroes(self):
        block = SOURCE.split("bool Instance::SendObjectCreate(", 1)[1].split(
            "void Instance::SendObjectDelete(", 1
        )[0]
        self.assertIn("noun->GetNonPlayerClassData()", block)
        self.assertIn("!object->IsPlayerControlled()", block)
        self.assertIn("RegisterSpawn(object->GetId())", block)

    def test_death_events_update_progress_and_beamout_scores(self):
        block = SOURCE.split("void Instance::OnObjectDeath(", 1)[1].split(
            "void Instance::OnPlayerStart(", 1
        )[0]
        self.assertIn("mEnemyClearTracker.RecordDeath(object->GetId())", block)
        self.assertIn("mEnemyClearTracker.GetKillPercent()", block)
        self.assertIn("SendObjectiveUpdate(client, static_cast<uint8_t>(i), 0)", block)
        beam = SOURCE.split("void Instance::BeamOut(", 1)[1].split(
            "bool Instance::SendObjectCreate(", 1
        )[0]
        self.assertIn("GetObjectivesForPlayer(player->GetId(), true)", beam)
        self.assertLess(beam.index("GetObjectivesForPlayer("),
                        beam.index("SendObjectivesComplete(client)"))

    def test_restart_clears_previous_planet_kills(self):
        startup = SOURCE.split("bool Instance::Start()", 1)[1].split(
            "void Instance::Stop()", 1
        )[0]
        self.assertIn("mEnemyClearTracker.Reset()", startup)


if __name__ == "__main__":
    unittest.main()
