"""Crash-prevention and gameplay regressions for core dungeon execution."""
from pathlib import Path
import unittest

SRC = Path(__file__).resolve().parents[1] / "darkspore_server/source"
GAME = (SRC / "game/instance.cpp").read_text(encoding="utf-8")
OBJECT = (SRC / "game/object.cpp").read_text(encoding="utf-8")
SERVER = (SRC / "raknet/server.cpp").read_text(encoding="utf-8")


class DungeonSafetyContracts(unittest.TestCase):
    def test_loot_creation_handles_missing_nouns(self):
        method = GAME.split("void Instance::DropLoot() {", 1)[1].split(
            "void Instance::DropLoot(const ObjectPtr&", 1
        )[0]
        self.assertIn("if (!object) {\n\t\t\t\tcontinue;", method)

    def test_random_catalyst_spawns_skip_failed_creates(self):
        method = GAME.split("void Instance::DropCatalyst() {", 1)[1].split(
            "void Instance::BeamOut(", 1
        )[0]
        self.assertLess(method.index("if (!object)"), method.index("object->SetPosition(position)"))

    def test_object_sync_guards_unbound_network_clients(self):
        method = GAME.split("void Instance::SendObjectUpdate(", 1)[1].split(
            "void Instance::SendAnimationState(", 1
        )[0]
        self.assertIn("if (!client)", method)
        self.assertIn("if (player && object ==", method)

    def test_healing_overflow_based_on_post_heal_value(self):
        method = OBJECT.split("std::tuple<float, bool> Object::Heal(", 1)[1].split(
            "bool Object::CheckCritical(", 1
        )[0]
        self.assertIn("health + value - maxHealth", method)
        self.assertIn("GetHealth() > 0.f", method)

    def test_dungeon_enters_with_player_selected_hero(self):
        method = SERVER.split("case GameState::Dungeon: {", 1)[1].split(
            "void Server::PrepareGameStart(", 1
        )[0]
        self.assertIn("mGame.SwapCharacter(player, player->GetCurrentDeckIndex())", method)
        self.assertNotIn("mGame.SwapCharacter(player, 1)", method)


if __name__ == "__main__":
    unittest.main()
