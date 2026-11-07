"""Chain player receipt retrieval and dungeon-start status must be state-safe."""
from pathlib import Path
import unittest

SOURCE = (Path(__file__).resolve().parents[1] /
          "darkspore_server/source/raknet/server.cpp").read_text(encoding="utf-8")


class ChainProtocolStateContracts(unittest.TestCase):
    def test_cashout_resend_is_safe_and_idempotent(self):
        block = SOURCE.split("void Server::OnChainPlayerMsgs(", 1)[1].split(
            "void Server::OnCrystalDragMessage(", 1
        )[0]
        self.assertIn("value == 4 && client->GetGameState() == GameState::ChainCashOut", block)
        after = block.split("value == 4 && client->GetGameState()", 1)[1]
        after = after.split("break;", 1)[0]
        self.assertIn("SendChainCashOutMessages(client, 0);", after)
        self.assertNotIn("user->Save()", after)
        self.assertNotIn("AddDna(", after)
        self.assertNotIn("SendReconnectPlayer(", after)

    def test_dungeon_start_requires_predungeon(self):
        block = SOURCE.split("void Server::OnPlayerStatusUpdate(", 1)[1].split(
            "void Server::OnActionCommandMsgs(", 1
        )[0]
        case = block.split("case 0x08: {", 1)[1].split("case 0x20:", 1)[0]
        self.assertIn("client->GetGameState() != GameState::PreDungeon", case)
        self.assertIn("SendGameStart(client);", case)
        self.assertIn("if (client->GetGameState() == GameState::Dungeon)", case)
        self.assertLess(case.index("SendGameStart(client);"), case.index("gameStateData.state"))


if __name__ == "__main__":
    unittest.main()
