"""Ensure creature customization survives account saves and missing templates."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "darkspore_server/source"


class CreaturePersistenceContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.creature = (ROOT / "sporenet/creature.cpp").read_text(encoding="utf-8")
        cls.h = (ROOT / "sporenet/creature.h").read_text(encoding="utf-8")
        cls.api = (ROOT / "game/api.cpp").read_text(encoding="utf-8")
        cls.user = (ROOT / "sporenet/user.cpp").read_text(encoding="utf-8")

    def test_missing_template_roundtrips_without_null_deref(self):
        self.assertIn("std::string mTemplateName;", self.h)
        self.assertIn('mTemplateName = node.attribute("template").as_string();', self.creature)
        self.assertIn('mTemplate ? mTemplate->GetName().c_str() : mTemplateName.c_str()', self.creature)

    def test_custom_stats_have_both_read_and_write_paths(self):
        read = self.creature.split("void Creature::Read(", 1)[1].split(
            "void Creature::Write(", 1
        )[0]
        write = self.creature.split("void Creature::Write(", 1)[1].split(
            "void Creature::WriteApi(", 1
        )[0]
        for tag in ("custom_stats", "custom_ability_stats"):
            self.assertIn(tag, read)
            self.assertIn(tag, write)

    def test_client_receives_actual_saved_stats(self):
        api_write = self.creature.split("void Creature::WriteApi(", 1)[1].split(
            "void Creature::Update(", 1
        )[0]
        self.assertNotIn('utils::xml_add_text_node(node, "stats", "TODO")', api_write)
        self.assertIn('"stats_ability_keyvalues"', api_write)

    def test_editor_update_rolls_back_when_save_fails(self):
        handler = self.api.split("void API::game_creature_updateCreature(", 1)[1].split(
            "void API::game_deck_updateDecks(", 1
        )[0]
        self.assertIn("const auto previousCreature = *creature;", handler)
        self.assertIn("success = user->Save();", handler)
        self.assertIn("*creature = previousCreature;", handler)
        self.assertIn("add_common_keys(docResponse, success);", handler)

    def test_unknown_unlock_does_not_consume_reward(self):
        func = self.user.split("uint32_t User::UnlockCreature(", 1)[1].split(
            "const std::vector<SquadPtr>& User::GetSquads", 1
        )[0]
        self.assertLess(func.index("mCreatures.Add(templateId)"),
                        func.index("--mAccount.creatureRewards"))


if __name__ == "__main__":
    unittest.main()
