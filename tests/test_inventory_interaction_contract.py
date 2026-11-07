"""Regressions for world interactions and equipment status persistence."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "darkspore_server/source"
GAME = (ROOT / "game/instance.cpp").read_text(encoding="utf-8")
API = (ROOT / "game/api.cpp").read_text(encoding="utf-8")
PARTS = (ROOT / "sporenet/part.cpp").read_text(encoding="utf-8")


class InteractionAndInventoryContracts(unittest.TestCase):
    def test_no_unlimited_loot_on_unknown_interactables(self):
        method = GAME.split("void Instance::InteractWithObject(", 1)[1].split(
            "void Instance::CancelAction(", 1
        )[0]
        self.assertNotIn("DropLoot(", method)
        self.assertIn("GetUsesAllowed()", method)
        self.assertIn("GetTimesUsed() >= interactableData->GetUsesAllowed()", method)
        self.assertLess(method.index("PickupLoot(player, object);"),
                        method.index("object->IsMarkedForDeletion() && interactableData"))

    def test_part_ids_are_one_based_and_bounded(self):
        self.assertIn("part.WriteApi(parts, ++index, offer);", PARTS)
        method = API.split("void API::game_inventory_updatePartStatus(", 1)[1].split(
            "void API::game_account_auth(", 1
        )[0]
        self.assertIn("partId == 0 || partId > parts.size()", method)
        self.assertIn("static_cast<size_t>(partId - 1)", method)
        self.assertNotIn("parts.data()[partIndex]", method)

    def test_updates_validate_before_mutation_and_rollback_on_save_failure(self):
        method = API.split("void API::game_inventory_updatePartStatus(", 1)[1].split(
            "void API::game_account_auth(", 1
        )[0]
        self.assertLess(method.index("std::from_chars("), method.index("SetStatus(status);"))
        self.assertLess(method.index("if (!user->Save())"), method.index("parts.data() = originalParts;"))
        self.assertIn("partIds.size() == statuses.size()", method)


if __name__ == "__main__":
    unittest.main()
