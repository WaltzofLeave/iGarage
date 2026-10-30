"""Run with the flask button, or: python -m unittest"""

import unittest
from utils import greeting, progress_bar


class TestUtils(unittest.TestCase):
    def test_greeting(self):
        self.assertIn("Orbit", greeting("Orbit"))

    def test_progress(self):
        self.assertIn("50%", progress_bar(1, 2))

    def test_empty_workspace(self):
        self.assertIn("No tasks", progress_bar(0, 0))

    def test_complete(self):
        self.assertIn("100%", progress_bar(3, 3))


if __name__ == "__main__":
    unittest.main(verbosity=2)
