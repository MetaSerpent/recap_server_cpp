"""Account and creature unlocks must survive restart or roll back."""
from pathlib import Path
import unittest

API = (Path(__file__).resolve().parents[1] /
       "darkspore_server/source/game/api.cpp").read_text(encoding="utf-8")


class RewardPersistenceContracts(unittest.TestCase):
    def test_upgrade_saves_and_restores_account_on_failure(self):
        method = API.split("void API::game_account_unlock(", 1)[1].split(
            "void API::game_account_setSettings(", 1
        )[0]
        self.assertIn("const auto previousAccount = user->get_account();", method)
        self.assertIn("if (success && !user->Save())", method)
        self.assertIn("user->get_account() = previousAccount;", method)
        self.assertIn("add_common_keys(docResponse, success);", method)

    def test_creature_unlock_is_durable_or_reverted(self):
        method = API.split("void API::game_creature_unlockCreature(", 1)[1].split(
            "void API::game_creature_getCreature(", 1
        )[0]
        self.assertIn("const auto previousRewards = user->get_account().creatureRewards;", method)
        self.assertIn("success = user->Save();", method)
        self.assertIn("user->get_account().creatureRewards = previousRewards;", method)
        self.assertIn("creatures.pop_back();", method)
        self.assertIn("creatureId = 0;", method)


if __name__ == "__main__":
    unittest.main()
