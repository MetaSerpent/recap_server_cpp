"""Regression contracts for bounded, atomic, persistent squad editing."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "darkspore_server/source"


class SquadPersistenceContracts(unittest.TestCase):
    def test_squad_slots_start_empty(self):
        hdr = (ROOT / "sporenet/squad.h").read_text(encoding="utf-8")
        self.assertIn("std::array<uint32_t, 3> mCreatureIds{};", hdr)

    def test_squad_edit_validates_before_writing(self):
        src = (ROOT / "sporenet/user.cpp").read_text(encoding="utf-8")
        block = src.split("bool User::UpdateSquad(", 1)[1].split(
            "bool User::UnlockUpgrade(", 1
        )[0]
        self.assertIn("SquadSelection::ParseCreatureIds", block)
        self.assertLess(block.index("if (creatureId != 0 && !mCreatures.Get(creatureId))"),
                        block.index("squad->SetCreatureIds(*ids, pvp)"))

    def test_deck_api_saves_and_rolls_back(self):
        api = (ROOT / "game/api.cpp").read_text(encoding="utf-8")
        block = api.split("void API::game_deck_updateDecks(", 1)[1].split(
            "void API::game_leaderboard_getLeaderboard(", 1
        )[0]
        self.assertIn("success = user->Save();", block)
        self.assertIn("*pveSquad = *beforePve;", block)
        self.assertIn("*pvpSquad = *beforePvp;", block)
        self.assertIn("add_common_keys(docResponse, success);", block)


if __name__ == "__main__":
    unittest.main()
