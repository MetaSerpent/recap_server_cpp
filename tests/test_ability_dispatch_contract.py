"""Ability execution is acknowledged only when Lua actually starts."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
IMPL = (ROOT / "darkspore_server/source/game/instance.cpp").read_text()
HEADER = (ROOT / "darkspore_server/source/game/instance.h").read_text()
NETWORK = (ROOT / "darkspore_server/source/raknet/server.cpp").read_text()


class AbilityDispatchContractTests(unittest.TestCase):
    def test_instance_reports_cast_result_and_forwards_tick_result(self):
        # UseAbility says why a cast did not start (game/cast_queue.h); only a
        # Tick that actually started the script counts as Started.
        self.assertIn("CastResult UseAbility(const ObjectPtr& object", HEADER)
        func = IMPL.split("CastResult Instance::UseAbility", 1)[1].split("void Instance::SwapCharacter", 1)[0]
        self.assertIn("return CastResult::Unavailable;", func)
        self.assertIn("return ability->Tick(", func)
        self.assertIn("? CastResult::Started : CastResult::Unavailable;", func)
        # no registered script is refused before any response is built
        self.assertLess(func.index("no registered ability script"), func.index("return ability->Tick("))

    def test_squad_does_not_ack_failed_execution(self):
        handler = NETWORK.split("// Squad ability", 1)[1].split("// Catalyst pickup", 1)[0]
        self.assertIn("const auto result = StartAbilityCast(client, object, command, abilityId); result != Game::CastResult::Started) {", handler)
        # StartAbilityCast sends Accepted only after the Lua script started
        start = NETWORK.split("Game::CastResult Server::StartAbilityCast(", 1)[1].split("void Server::HandleCharacterAbility(", 1)[0]
        self.assertIn("if (result != Game::CastResult::Started) {\n\t\t\treturn result;", start)
        self.assertLess(start.index("mGame.UseAbility(object, combatData)"), start.index("SendActionCommandResponse("))


if __name__ == "__main__":
    unittest.main()
