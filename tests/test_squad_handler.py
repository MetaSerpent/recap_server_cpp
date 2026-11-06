from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "darkspore_server/source/raknet/server.cpp"


class SquadHandlerContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        text = SOURCE.read_text(encoding="utf-8")
        cls.handler = text.split("// Squad ability", 1)[1].split("// Catalyst pickup", 1)[0]

    def test_only_three_defined_squad_slots(self):
        self.assertIn("IsSquadAbility(command.ability.index)", self.handler)

    def test_resolve_from_party_and_dispatch_lua(self):
        self.assertIn("player->GetAbilityId(0xFF", self.handler)
        self.assertIn("mGame.GetLua().GetAbility(abilityId)", self.handler)
        self.assertIn("mGame.UseAbility(object, combatData)", self.handler)

    def test_no_fabricated_boss_completion_or_cooldown(self):
        self.assertNotIn("mbBossComplete", self.handler)
        self.assertNotIn("response.cooldown = 2000", self.handler)

    def test_preserve_original_response_identifier(self):
        self.assertIn("response.userData = command.ability.userData", self.handler)


if __name__ == "__main__":
    unittest.main()
