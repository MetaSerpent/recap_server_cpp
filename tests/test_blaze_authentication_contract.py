"""Protect Blaze account authentication from cached-session bypasses."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "darkspore_server/source"


class BlazeAuthenticationContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.users = (ROOT / "sporenet/user.cpp").read_text(encoding="utf-8")
        cls.blaze = (ROOT / "blaze/component/authcomponent.cpp").read_text(encoding="utf-8")

    def test_cached_session_validates_password_before_acceptance(self):
        login = self.users.split("UserManager::Login(", 1)[1].split(
            "UserManager::GetUsers(", 1
        )[0]
        self.assertLess(login.index("it->second->get_password() == password"),
                        login.index("alreadyLoggedIn = true;"))

    def test_unauthenticated_express_login_uses_password_check(self):
        express = self.blaze.split("void AuthComponent::ExpressLogin(", 1)[1].split(
            "void AuthComponent::LoginPersona(", 1
        )[0]
        self.assertIn("GetUserManager().Login(username, password)", express)
        self.assertIn("(!success && !alreadyLoggedIn)", express)
        self.assertNotIn("GetUserByEmail(username)", express)

    def test_empty_tokens_are_never_valid_credentials(self):
        check = self.users.split("UserManager::GetUserByAuthToken(", 1)[1].split(
            "UserManager::RemoveUser(", 1
        )[0]
        self.assertIn("if (authToken.empty())", check)

    def test_failed_silent_login_never_prints_token(self):
        silent = self.blaze.split("void AuthComponent::SilentLogin(", 1)[1].split(
            "void AuthComponent::ExpressLogin(", 1
        )[0]
        self.assertNotIn('<< authToken', silent)


if __name__ == "__main__":
    unittest.main()
