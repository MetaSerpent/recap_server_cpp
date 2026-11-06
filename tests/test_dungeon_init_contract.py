"""Ensure repeated dungeon pings do not reset player/hero state."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
HEADER = (ROOT / "darkspore_server/source/raknet/client.h").read_text()
CLIENT = (ROOT / "darkspore_server/source/raknet/client.cpp").read_text()
SERVER = (ROOT / "darkspore_server/source/raknet/server.cpp").read_text()


class DungeonEntryContractTests(unittest.TestCase):
    def test_per_client_flag_exists(self):
        self.assertIn("bool mDungeonInitialized = false;", HEADER)
        self.assertIn("bool MarkDungeonInitialized();", HEADER)

    def test_game_state_transition_resets_flag(self):
        transition = CLIENT.split("bool Client::SetGameState", 1)[1].split("bool Client::MarkDungeonInitialized", 1)[0]
        self.assertIn("if (newState != mGameState)", transition)
        self.assertIn("mDungeonInitialized = false;", transition)

    def test_marking_dungeon_twice_only_initializes_once(self):
        fn = CLIENT.split("bool Client::MarkDungeonInitialized()", 1)[1].split("SporeNet::UserPtr Client::GetUser", 1)[0]
        self.assertIn("mGameState != GameState::Dungeon || mDungeonInitialized", fn)
        self.assertIn("mDungeonInitialized = true;", fn)
        self.assertIn("return true;", fn)

    def test_ping_gates_resets(self):
        ping = SERVER.split("void Server::OnDebugPing", 1)[1].split("void Server::PrepareGameStart", 1)[0]
        case = ping.split("case GameState::Dungeon", 1)[1]
        self.assertIn("!client->MarkDungeonInitialized()", case)
        self.assertLess(case.index("!client->MarkDungeonInitialized()"), case.index("mGame.OnPlayerStart(player)"))


if __name__ == "__main__":
    unittest.main()
