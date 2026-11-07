"""Ability ranks and tutorial locks must respect actual nine-slot bounds."""
from pathlib import Path
import unittest

SRC = Path(__file__).resolve().parents[1] / "darkspore_server/source/game"


class AbilityBoundsContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.player = (SRC / "player.cpp").read_text(encoding="utf-8")
        cls.object = (SRC / "object.cpp").read_text(encoding="utf-8")

    def test_ability_rank_rejects_foreign_hero_or_ability_slot(self):
        method = self.player.split("uint32_t Player::GetAbilityRank(", 1)[1].split(
            "bool Player::IsAbilityLocked(", 1
        )[0]
        self.assertIn("AbilitySelection::IsValidHeroSlot", method)
        self.assertIn("AbilitySelection::IsValidAbilityIndex", method)
        self.assertNotIn("std::clamp", method)

    def test_default_lock_sentinel_keeps_abilities_available(self):
        method = self.player.split("bool Player::IsAbilityLocked(", 1)[1].split(
            "bool Player::IsOverdriveCharged(", 1
        )[0]
        self.assertIn("if (mLockedAbilityMin == 0xFF)", method)
        self.assertIn("return false;", method)

    def test_ability_use_checks_index_and_rank_before_squad_lookup(self):
        method = self.object.split("int32_t Object::CanUseAbility(", 1)[1].split(
            "void Object::RequestAbility(", 1
        )[0]
        self.assertIn("rank < 0 || rank > 7", method)
        self.assertIn("AbilitySelection::IsValidAbilityIndex(abilityIndex)", method)
        self.assertLess(method.index("IsValidAbilityIndex(abilityIndex)"),
                        method.index("player->GetCharacter(abilityIndex - 6)"))


if __name__ == "__main__":
    unittest.main()
