"""Protect AI logic against empty or changed combat gambit definitions."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "darkspore_server/source/game"


class AIGambitContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cpp = (ROOT / "object.cpp").read_text(encoding="utf-8")
        cls.h = (ROOT / "object.h").read_text(encoding="utf-8")

    def test_empty_gambits_rejected_before_index(self):
        block = self.cpp.split("void AI::UseAbility() {", 1)[1].split(
            "// Object", 1
        )[0]
        self.assertLess(block.index("if (gambit.empty())"), block.index("gambit[mGambit]"))
        self.assertIn("AIGambitSelection::NormalizeIndex(mGambit, gambit.size())", block)

    def test_random_index_avoids_eight_bit_truncation(self):
        self.assertIn("utils::random::get<std::size_t>(0, gambit.size() - 1)", self.cpp)
        self.assertIn("std::size_t mGambit = 0;", self.h)

    def test_ai_does_not_cast_with_null_target(self):
        block = self.cpp.split("bool AI::UseAbility(uint32_t id)", 1)[1].split(
            "void AI::UseAbility()", 1
        )[0]
        self.assertIn("!mTargetObject", block)
        self.assertIn("mTargetObject->GetHealth() <= 0.f", block)


if __name__ == "__main__":
    unittest.main()
