"""Regression checks for persistent built-in rooms and correct membership transfer."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
ROOM = (ROOT / "darkspore_server/source/sporenet/room.cpp").read_text()
HEADER = (ROOT / "darkspore_server/source/sporenet/room.h").read_text()


class RoomMembershipContractTests(unittest.TestCase):
    def test_permanent_flag_defaults_false(self):
        self.assertIn("bool mPermanent = false;", HEADER)
        ctor = ROOM.split("RoomManager::RoomManager()", 1)[1].split("RoomPtr RoomManager::GetRoom", 1)[0]
        self.assertIn("room->mPermanent = true;", ctor)

    def test_room_transfers_unlink_old_membership(self):
        add = ROOM.split("void Room::AddUser", 1)[1].split("void Room::RemoveUser", 1)[0]
        self.assertIn("previousRoom->RemoveUser(user);", add)
        self.assertLess(add.index("previousRoom->RemoveUser(user)"), add.index("user->SetRoom(shared_from_this())"))
        self.assertIn("mUsers.insert_or_assign(user->get_id(), user)", add)

    def test_only_dynamic_empty_rooms_removed(self):
        remove = ROOM.split("void Room::RemoveUser", 1)[1].split("void Room::WriteTo", 1)[0]
        self.assertIn("user->SetRoom(nullptr)", remove)
        self.assertIn("mUsers.empty() && !mPermanent", remove)


if __name__ == "__main__":
    unittest.main()
