"""Ability execution is acknowledged only when Lua actually starts."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
IMPL = (ROOT / "darkspore_server/source/game/instance.cpp").read_text()
HEADER = (ROOT / "darkspore_server/source/game/instance.h").read_text()
NETWORK = (ROOT / "darkspore_server/source/raknet/server.cpp").read_text()


class AbilityDispatchContractTests(unittest.TestCase):
    def test_instance_returns_bool_and_forwards_tick_result(self):
        self.assertIn("bool UseAbility(const ObjectPtr& object", HEADER)
        func = IMPL.split("bool Instance::UseAbility", 1)[1].split("void Instance::SwapCharacter", 1)[0]
        self.assertIn("return false;", func)
        self.assertIn("return ability->Tick(", func)

    def test_squad_does_not_ack_failed_execution(self):
        handler = NETWORK.split("// Squad ability", 1)[1].split("// Catalyst pickup", 1)[0]
        self.assertIn("if (!mGame.UseAbility(object, combatData))", handler)
        self.assertLess(handler.index("if (!mGame.UseAbility"), handler.index("SendActionCommandResponse("))


if __name__ == "__main__":
    unittest.main()
