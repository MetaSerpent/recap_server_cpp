"""The Windows server must not force redirected logs back to the console."""
from pathlib import Path
import unittest

MAIN = (Path(__file__).resolve().parents[1] / "darkspore_server/source/main.cpp").read_text(encoding="utf-8")


class LogRedirectionContract(unittest.TestCase):
    def test_console_reopen_only_when_not_redirected(self):
        init = MAIN[MAIN.index("bool Application::OnInit()"):MAIN.index("Starting recap server v")]
        self.assertIn("type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE", init)
        self.assertIn('if (!stdoutRedirected) {\n\t\t(void)freopen("CONOUT$", "w", stdout);', init)
        self.assertIn('if (!stderrRedirected) {\n\t\t(void)freopen("CONOUT$", "w", stderr);', init)


if __name__ == "__main__":
    unittest.main()
