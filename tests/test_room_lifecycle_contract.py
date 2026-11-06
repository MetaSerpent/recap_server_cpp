"""Ensure SporeNet room-map teardown cannot recursively erase its own node."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
ROOM = (ROOT / "darkspore_server/source/sporenet/room.cpp").read_text()


class RoomLifecycleTests(unittest.TestCase):
    def test_owned_rooms_do_not_mutate_manager_in_destructor(self):
        for kind in ("Room", "RoomCategory", "RoomView"):
            with self.subTest(kind=kind):
                start = ROOM.index(kind + "::~" + kind + "()")
                block = ROOM[start:ROOM.index("}", start) + 1] if "{" in ROOM[start:start + 90] else ROOM[start:start + 56]
                self.assertNotIn("mManager.Remove", block)
                self.assertIn(kind + "::~" + kind + "() = default;", ROOM)

    def test_explicit_removal_routes_remain_available(self):
        self.assertIn("mManager.RemoveRoom(mId);", ROOM)
        for kind in ("Room", "RoomCategory", "RoomView"):
            self.assertIn("void RoomManager::Remove" + kind + "(uint32_t id)", ROOM)


if __name__ == "__main__":
    unittest.main()
