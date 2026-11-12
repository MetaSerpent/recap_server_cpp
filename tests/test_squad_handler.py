from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "darkspore_server/source/raknet/server.cpp"


class SquadHandlerContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        text = SOURCE.read_text(encoding="utf-8")
        cls.text = text
        cls.handler = text.split("// Squad ability", 1)[1].split("// Catalyst pickup", 1)[0]

    def test_only_three_defined_squad_slots(self):
        self.assertIn("IsSquadAbility(command.ability.index)", self.handler)

    def test_resolve_from_party_and_dispatch_lua(self):
        self.assertIn("player->GetAbilityId(0xFF", self.handler)
        self.assertIn("StartAbilityCast(client, object, command, abilityId)", self.handler)
        # the shared cast path dispatches to Lua; unscripted abilities are refused there
        start = self.text.split("Game::CastResult Server::StartAbilityCast(", 1)[1].split("void Server::HandleCharacterAbility(", 1)[0]
        self.assertIn("mGame.UseAbility(object, combatData)", start)
        instance = (ROOT / "darkspore_server/source/game/instance.cpp").read_text(encoding="utf-8")
        use = instance.split("CastResult Instance::UseAbility(", 1)[1].split("void Instance::SwapCharacter", 1)[0]
        self.assertIn("mLua->GetAbility(combatData.abilityId)", use)

    def test_no_fabricated_boss_completion_or_cooldown(self):
        self.assertNotIn("mbBossComplete", self.handler)
        self.assertNotIn("response.cooldown = 2000", self.handler)

    def test_preserve_original_response_identifier(self):
        # Build-103 responses identify the cast by the command's sync stamp
        # (raknet/action_response.h); the command's user data still reaches Lua.
        self.assertIn("ActionResponse::Reject(command.data.unk[0], command.data.objectId)", self.handler)
        start = self.text.split("Game::CastResult Server::StartAbilityCast(", 1)[1].split("void Server::HandleCharacterAbility(", 1)[0]
        self.assertIn("ActionResponse::Type::Accepted, command.data.unk[0], abilityId", start)
        self.assertIn("combatData.valueFromActionResponse = command.ability.userData;", start)


if __name__ == "__main__":
    unittest.main()
