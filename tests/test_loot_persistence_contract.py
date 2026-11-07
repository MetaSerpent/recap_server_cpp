"""Equipment pickups must actually persist inventory before consuming loot."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "darkspore_server/source"


class LootPersistenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.object_header = (ROOT / "game/object.h").read_text()
        cls.object_source = (ROOT / "game/object.cpp").read_text()
        cls.instance = (ROOT / "game/instance.cpp").read_text()

    def test_original_part_is_retained_in_world_loot(self):
        self.assertIn("std::shared_ptr<SporeNet::Part> mPart;", self.object_header)
        self.assertIn("mPart = std::make_shared<SporeNet::Part>(part)", self.object_source)

    def test_equipment_is_saved_before_consuming_the_world_item(self):
        block = self.instance.split("void Instance::PickupLoot(", 1)[1].split(
            "void Instance::PickupCatalyst(", 1
        )[0]
        self.assertLess(block.index("user->AddPart(*part)"), block.index("user->Save()"))
        self.assertLess(block.index("user->Save()"), block.index("object->MarkForDeletion()"))
        self.assertIn("user->get_parts().data().pop_back()", block)
        self.assertIn("lootData->GetPart()", block)
        self.assertIn("lootEvent.SetLootPickup", block)
