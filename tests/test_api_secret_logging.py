"""Ensure authentication secrets are not dumped by the game API.

This is a source-level guard; the headless integration test exercises the
public API itself without using real player credentials.
"""
from pathlib import Path
import unittest

API = Path(__file__).resolve().parents[1] / "darkspore_server/source/game/api.cpp"


class SecretLoggingContracts(unittest.TestCase):
    def test_request_parameters_are_not_dumped(self):
        content = API.read_text(encoding="utf-8")
        api_route = content.split('router->add("/game/api"', 1)[1].split(
            'router->add("/creature_png/', 1
        )[0]
        self.assertNotIn("std::cout << name <<", api_route)
        self.assertIn("request.uri.parameter(\"token\")", api_route)


if __name__ == "__main__":
    unittest.main()
