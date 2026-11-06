"""Verify safe extraction and preservation of existing desktop releases."""
from pathlib import Path
import tempfile
import unittest
import zipfile

from package_portable import ROOT, extract_portable


class DeliveryTests(unittest.TestCase):
    def setUp(self):
        build = (ROOT / "build").resolve()
        build.mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="delivery-test-", dir=build)
        self.root = Path(self.temporary.name).resolve()
        assert self.root.parent == build  # Cleanup is restricted to this owned build fixture.
        self.addCleanup(self.temporary.cleanup)

    def archive(self, extra=()):
        path = self.root / "portable.zip"
        with zipfile.ZipFile(path, "w") as archive:
            archive.writestr("OrbitNative.exe", b"fixture executable")
            archive.writestr("services/pyrefly/LICENSE.txt", b"fixture license")
            for name in extra:
                info = zipfile.ZipInfo(name)
                info.filename = name  # Preserve malformed backslashes that ZipInfo normally normalizes on Windows.
                archive.writestr(info, b"fixture content")
        return path

    def test_extracts_ready_folder_with_backend_notice(self):
        folder = extract_portable(self.archive(), self.root / "desktop")
        self.assertEqual((folder / "OrbitNative.exe").read_bytes(), b"fixture executable")
        self.assertTrue((folder / "services/pyrefly/LICENSE.txt").is_file())
        self.assertFalse((folder / "data/session.json").exists())

    def test_second_delivery_preserves_existing_release_and_project(self):
        archive = self.archive()
        first = extract_portable(archive, self.root / "desktop")
        (first / "my-project.py").write_text("keep me", encoding="utf-8")
        second = extract_portable(archive, self.root / "desktop")
        self.assertNotEqual(first, second)
        self.assertEqual((first / "my-project.py").read_text(encoding="utf-8"), "keep me")

    def test_rejects_parent_traversal_before_creating_release(self):
        with self.assertRaises(ValueError):
            extract_portable(self.archive(["../outside.py"]), self.root / "desktop")
        self.assertFalse((self.root / "outside.py").exists())
        self.assertFalse((self.root / "desktop").exists())

    def test_rejects_windows_absolute_and_backslash_names(self):
        for name in ["C:/outside.py", "src\\outside.py", "/outside.py"]:
            with self.subTest(name=name), self.assertRaises(ValueError):
                extract_portable(self.archive([name]), self.root / "desktop")

    def test_rejects_names_that_collide_on_windows(self):
        with self.assertRaises(ValueError):
            extract_portable(self.archive(["ORBITNATIVE.EXE"]), self.root / "desktop")


if __name__ == "__main__":
    unittest.main()
