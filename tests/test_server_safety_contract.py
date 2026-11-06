"""Static regression checks for severe disconnect, packet, and iterator crashes.

These are source-contract tests; actual client integration is still required.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
NET = (ROOT / "darkspore_server/source/raknet/server.cpp").read_text()
GAME = (ROOT / "darkspore_server/source/game/instance.cpp").read_text()
OBJECTS = (ROOT / "darkspore_server/source/game/objectmanager.cpp").read_text()


class ServerSafetyTests(unittest.TestCase):
    def test_queued_task_uses_player_id_not_first_connection(self):
        task = NET.split("void Server::add_client_task", 1)[1].split("void Server::", 1)[0]
        self.assertIn("const auto client = GetClient(id);", task)
        self.assertNotIn("mClients.begin()->second", task)

    def test_reject_malformed_timestamp_before_parsing(self):
        parser = NET.split("void Server::run_one()", 1)[1].split("void Server::add_task", 1)[0]
        self.assertIn("packet->bitSize < 8", parser)
        self.assertIn("packet->data[0] == ID_TIMESTAMP", parser)
        self.assertNotIn("RakAssert(", parser)

    def test_trigger_erasure_does_not_skip_next_element(self):
        handler = OBJECTS.split("void TriggerVolume::OnTick", 1)[1].split("const ObjectPtr& TriggerVolume::GetOwnerObject", 1)[0]
        self.assertNotIn("mObjectStates.end(); ++it)", handler)
        self.assertIn("it = mObjectStates.erase(it);", handler)
        self.assertIn("continue;", handler)
        self.assertIn("++it;", handler)

    def test_loot_uses_active_hero_and_null_guard(self):
        drop = GAME.split("void Instance::DropLoot()", 1)[1].split("void Instance::DropLoot(", 1)[0]
        self.assertIn("if (!firstClient)", drop)
        self.assertIn("if (!player)", drop)
        self.assertIn("GetDeployedCharacterObject()", drop)
        self.assertNotIn("GetCharacterObject(1)", drop)

    def test_catalyst_handles_disconnected_first_client(self):
        drop = GAME.split("void Instance::DropCatalyst()", 1)[1].split("void Instance::BeamOut", 1)[0]
        self.assertIn("if (!firstClient)", drop)
        self.assertIn("if (!player)", drop)
        self.assertIn("if (!characterObject)", drop)


if __name__ == "__main__":
    unittest.main()
