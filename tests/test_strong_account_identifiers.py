"""Prevent account-ID-derived tokens and same-second account-ID collisions."""
from pathlib import Path
import unittest

SRC = Path(__file__).resolve().parents[1] / "darkspore_server/source"


class StrongAccountIdentifiers(unittest.TestCase):
    def test_auth_token_uses_csprng_and_not_public_account_id(self):
        src = (SRC / "blaze/component/authcomponent.cpp").read_text(encoding="utf-8")
        method = src.split("void AuthComponent::GetAuthToken(", 1)[1].split(
            "void AuthComponent::Login(", 1
        )[0]
        self.assertIn("RAND_bytes(", method)
        self.assertIn("SporeNet::Auth::EncodeToken(randomBytes)", method)
        self.assertNotIn("std::to_string(user->get_id())", method)

    def test_registration_does_not_reseed_rand_per_request(self):
        src = (SRC / "sporenet/user.cpp").read_text(encoding="utf-8")
        registration = src.split("UserManager::SignUp(", 1)[1].split(
            "UserManager::Login(", 1
        )[0]
        self.assertIn("RAND_bytes(", registration)
        self.assertNotIn("srand(time(NULL))", registration)

    def test_registration_checks_final_save(self):
        src = (SRC / "game/api.cpp").read_text(encoding="utf-8")
        registration = src.split("void API::recap_game_registration(", 1)[1].split(
            "void API::recap_game_status(", 1
        )[0]
        self.assertIn('if (!user->Save())', registration)


if __name__ == "__main__":
    unittest.main()
