"""Static regression contracts for crash and gameplay progression fixes.

These assertions protect the exact call sites until integration fixtures
can exercise full game objects and the proprietary client protocol.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "darkspore_server/source/game"


class GameplayRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.instance = (ROOT / "instance.cpp").read_text(encoding="utf-8")
        cls.player = (ROOT / "player.cpp").read_text(encoding="utf-8")
        cls.object = (ROOT / "object.cpp").read_text(encoding="utf-8")

    def test_enemy_spawners_do_not_stop_after_first_marker(self):
        block = self.instance.split("const auto TestEnemy =", 1)[1].split(
            "if (mLevel.GetMarkerset(levelName + \"_AI_Wander.Markerset\"", 1
        )[0]
        self.assertIn("for (const auto& marker : markerset.GetMarkers())", block)
        self.assertNotIn("// temporary", block)
        self.assertNotIn("Unknown enemy noun:", block)
        self.assertEqual(block.count("break;"), 2)  # switch cases only

    def test_missing_noun_does_not_crash_level_load(self):
        self.assertEqual(
            self.instance.count("if (noun && noun->GetNonPlayerClassData())"), 2
        )

    def test_loot_payload_checked_before_consuming_world_object(self):
        block = self.instance.split("void Instance::PickupLoot(", 1)[1].split(
            "void Instance::PickupCatalyst(", 1
        )[0]
        self.assertLess(block.index("if (!lootData)"), block.index("object->MarkForDeletion();"))

    def test_catalyst_payload_checked_before_consuming_world_object(self):
        block = self.instance.split("void Instance::PickupCatalyst(", 1)[1]
        self.assertLess(block.index("if (!lootData)"), block.index("object->MarkForDeletion();"))

    def test_catalyst_bounds_and_death_transition(self):
        self.assertIn("if (!IsValidCatalystSlot(index))", self.player)
        self.assertIn("healthBeforeHit > 0.f && GetHealth() <= 0.f", self.object)


if __name__ == "__main__":
    unittest.main()
