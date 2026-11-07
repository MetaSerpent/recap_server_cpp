"""Obelisk objective is driven by real, one-time world interactions."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "darkspore_server"
CPP = (ROOT / "source/game/instance.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "source/game/instance.h").read_text(encoding="utf-8")
SCORING = (ROOT / "source/game/objective_scoring.h").read_text(encoding="utf-8")
LUA = (ROOT / "res/data/serverdata/lua/objective_Obelisks.lua").read_text(encoding="utf-8")


class ObeliskObjectiveContract(unittest.TestCase):
    def test_original_thresholds(self):
        for name, number in (("goldObelisks", "3"), ("silverObelisks", "2"),
                             ("bronzeObelisks", "1")):
            self.assertIn(name + " = " + number, LUA)
        self.assertIn("ScoreTouchedObelisks(", SCORING)

    def test_interaction_is_one_time_and_updates_the_wire(self):
        method = CPP.split("void Instance::InteractWithObject(", 1)[1].split(
            "void Instance::CancelAction(", 1
        )[0]
        self.assertIn('GetAbility() == utils::hash_id("InteractWithObelisk")', method)
        self.assertIn("mTouchedObelisks.insert(objectId).second", method)
        self.assertIn("interactableData->SetTimesUsed(", method)
        self.assertIn("SendInteractableDataUpdate(client, object, *interactableData)", method)
        self.assertIn("SendObjectiveUpdate(client, static_cast<uint8_t>(i)", method)
        self.assertNotIn("DropLoot(", method)

    def test_score_happens_before_completion_packet(self):
        method = CPP.split("void Instance::BeamOut(", 1)[1].split(
            "bool Instance::SendObjectCreate(", 1
        )[0]
        self.assertIn("ObjectiveScoring::ScoreTouchedObelisks", method)
        self.assertLess(method.index("ObjectiveScoring::ScoreTouchedObelisks"),
                        method.index("SendObjectivesComplete(client);"))

    def test_restart_clears_touch_history(self):
        self.assertIn("std::unordered_set<uint32_t> mTouchedObelisks;", HEADER)
        startup = CPP.split("bool Instance::Start() {", 1)[1].split(
            "void Instance::Stop() {", 1
        )[0]
        self.assertIn("mTouchedObelisks.clear();", startup)


if __name__ == "__main__":
    unittest.main()
