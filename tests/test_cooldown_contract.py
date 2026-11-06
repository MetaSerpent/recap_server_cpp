from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
OBJECT = (ROOT / "darkspore_server/source/game/object.cpp").read_text()
LUA = (ROOT / "darkspore_server/source/game/luafunctions.cpp").read_text()


class CooldownIntegrationContractTests(unittest.TestCase):
    def test_object_scales_remaining_cooldown(self):
        function = OBJECT.split("Cooldown Object::ScaleCooldown", 1)[1].split("// Properties", 1)[0]
        self.assertIn("ScaleRemainingMilliseconds", function)
        self.assertIn("mCooldowns.erase(it)", function)
        self.assertIn("std::make_tuple(now + remaining, remaining)", function)

    def test_lua_scale_cooldown_sends_client_update(self):
        function = LUA.split("auto ScaleCooldown(sol::this_state", 1)[1].split("auto TakeDamage", 1)[0]
        self.assertIn("object->ScaleCooldown(abilityId, scale)", function)
        self.assertIn("game.SendCooldownUpdate(", function)
        self.assertIn("end - duration, duration", function)


if __name__ == "__main__":
    unittest.main()
