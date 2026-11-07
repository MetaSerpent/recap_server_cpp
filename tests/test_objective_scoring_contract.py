"""Check original speed medal rules and level-entry clock reset."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
GAME = (ROOT / "darkspore_server/source/game/instance.cpp").read_text(encoding="utf-8")
RULES = (ROOT / "darkspore_server/source/game/objective_scoring.h").read_text(encoding="utf-8")
LUA = (ROOT / "darkspore_server/res/data/serverdata/lua/Objective_FinishLevelQuickly.lua").read_text(encoding="utf-8")

class ObjectiveScoringContract(unittest.TestCase):
    def test_exact_source_thresholds(self):
        self.assertIn("L0_1.goldTime = 600", LUA)
        self.assertIn("L0_1.silverTime = 900", LUA)
        self.assertIn("L0_1.bronzeTime = 1200", LUA)
        for seconds in ("600", "900", "1200"):
            self.assertIn(f"elapsedSeconds <= {seconds}", RULES)

    def test_clock_resets_on_first_level_entry(self):
        entry = GAME.split("void Instance::OnPlayerStart(", 1)[1].split(
            "uint32_t Instance::AddTask(", 1
        )[0]
        self.assertIn("if (initializeWorld)", entry)
        self.assertIn("mGameStartTime = utils::get_milliseconds();", entry)
        self.assertIn("mGameTime = mGameStartTime;", entry)

    def test_medals_not_prematurely_gold(self):
        constructor = GAME.split("Instance::Instance(uint32_t id)", 1)[1].split(
            "Instance::~Instance()", 1
        )[0]
        self.assertIn("objective.medal = RakNet::ObjectiveMedal::InProgress;", constructor)
        self.assertNotIn("objective.medal = RakNet::ObjectiveMedal::Gold;", constructor)

    def test_completion_scores_from_real_elapsed_time(self):
        method = GAME.split("void Instance::BeamOut(", 1)[1].split(
            "void Instance::SendObjectCreate(", 1
        )[0]
        self.assertIn("ObjectiveScoring::ScoreFinishLevelQuickly(elapsedSeconds)", method)
        self.assertLess(method.index("ScoreFinishLevelQuickly(elapsedSeconds)"),
                        method.index("SendObjectivesComplete(client);"))

if __name__ == "__main__":
    unittest.main()
