"""Catalyst moves must never lose items on invalid destinations or failed drops."""
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[1] / "darkspore_server/source"
class CatalystMoveTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = (ROOT / "raknet/server.cpp").read_text(encoding="utf-8")
        cls.instance = (ROOT / "game/instance.cpp").read_text(encoding="utf-8")
        cls.player = (ROOT / "game/player.cpp").read_text(encoding="utf-8")
    def test_slot_validation_precedes_swap(self):
        block = self.server.split("void Server::OnCrystalDragMessage(", 1)[1].split("void Server::OnLootDropMessage(", 1)[0]
        self.assertIn("!player->IsValidCatalystSlot(crystalSlot)", block)
        self.assertIn("!player->IsValidCatalystSlot(newSlot)", block)
        self.assertLess(block.index("!player->IsValidCatalystSlot(newSlot)"), block.index("player->SetCatalyst(catalyst, newSlot)"))
    def test_drop_acknowledgment_depends_on_creation(self):
        block = self.server.split("void Server::OnCrystalDragMessage(", 1)[1].split("void Server::OnLootDropMessage(", 1)[0]
        self.assertIn("if (mGame.DropCatalyst(player, crystalSlot))", block)
    def test_inventory_cleared_only_after_world_item_creation(self):
        block = self.instance.split("bool Instance::DropCatalyst(", 1)[1].split("void Instance::DropCatalyst()", 1)[0]
        self.assertLess(block.index("if (!SendObjectCreate(object))"), block.index("player->SetCatalyst(Catalyst(), catalystSlot)"))
        self.assertIn("mServer->SendLootDataUpdate(client, object, *lootData)", block)
        self.assertIn("mServer->SendInteractableDataUpdate(client, object, *interactableData)", block)
    def test_bounds_check_is_owned_by_player(self):
        self.assertIn("bool Player::IsValidCatalystSlot(uint32_t index) const", self.player)
        self.assertIn("return index < mCatalysts.size();", self.player)
