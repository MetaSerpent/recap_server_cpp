"""Regression contracts for server startup/shutdown resource lifetimes."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
MAIN = (ROOT / "darkspore_server/source/main.cpp").read_text()
HEADER = (ROOT / "darkspore_server/source/main.h").read_text()
CONFIG = (ROOT / "darkspore_server/source/game/config.cpp").read_text()


class StartupShutdownTests(unittest.TestCase):
    def test_data_loader_is_owned_and_joined(self):
        self.assertIn("std::thread mDataLoaderThread;", HEADER)
        self.assertIn("mDataLoaderThread = std::thread(", MAIN)
        self.assertNotIn("t.detach();", MAIN)
        exit_code = MAIN.split("int Application::OnExit()", 1)[1].split("void Application::Run()", 1)[0]
        self.assertIn("mDataLoaderThread.joinable()", exit_code)
        self.assertIn("mDataLoaderThread.join();", exit_code)
        self.assertLess(exit_code.index("mDataLoaderThread.join();"), exit_code.index("mGameAPI.reset();"))

    def test_all_created_services_are_released(self):
        exit_code = MAIN.split("int Application::OnExit()", 1)[1].split("void Application::Run()", 1)[0]
        for name in ("mRedirectorServer", "mBlazeServer", "mPssServer", "mTickServer",
                     "mTelemetryServer", "mHttpServer", "mHttpTelemetryServer",
                     "mHttpQosServer", "mQosServer", "mSporeNet"):
            with self.subTest(service=name):
                self.assertIn(name + ".reset();", exit_code)

    def test_empty_storage_path_is_safe(self):
        getter = CONFIG.split("const auto get_path_value", 1)[1].split("const auto parse_config_value", 1)[0]
        self.assertIn("if (value.empty())", getter)
        self.assertIn('value = "storage/";', getter)
        self.assertLess(getter.index("if (value.empty())"), getter.index("value.back()"))


if __name__ == "__main__":
    unittest.main()
