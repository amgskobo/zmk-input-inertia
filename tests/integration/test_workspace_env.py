# SPDX-License-Identifier: MIT
"""Exercise actual workspace selection with cache links and a poisoned ambient list."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HELPER = ROOT / "tests/integration/workspace-env.sh"

class SelectedWorkspace(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="selected-workspace-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.workspace = self.root / "selected workspace"
        self.workspace.mkdir()
        cache = self.root / "other cache/zephyr"
        cache.mkdir(parents=True)
        (self.workspace / "zephyr").symlink_to(cache, target_is_directory=True)
        self.modules = [self.root / "module one", self.root / "external module two"]
        for module in self.modules:
            (module / "zephyr").mkdir(parents=True)
        (self.modules[0] / "zephyr/module.yml").write_text("name: first\n")
        for name in ("CMakeLists.txt", "Kconfig"):
            (self.modules[1] / "zephyr" / name).touch()
        bindir = self.root / "bin"
        bindir.mkdir()
        west = bindir / "west"
        west.write_text("#!/usr/bin/env python3\nimport os,sys\nprint(os.environ.get('TEST_PROJECTS',''))\nsys.exit(int(os.environ.get('TEST_WEST_EXIT','0')))\n")
        west.chmod(0o755)
        self.env = {**os.environ, "PATH": str(bindir) + os.pathsep + os.environ["PATH"],
            "ZEPHYR_BASE": "ambient/zephyr", "Zephyr_DIR": "ambient/package",
            "ZEPHYR_MODULES": "ambient/incorrect-provider",
            "TEST_PROJECTS": "\n".join(map(str, [*self.modules, self.root / "not-a-module"]))}

    def run_selection(self):
        command = 'set -e; source "$1"; zmk_test_select_workspace "$2"; python3 -c \'import os,json; print(json.dumps({k:os.environ.get(k) for k in ["ZEPHYR_BASE","Zephyr_DIR","ZEPHYR_MODULES"]}))\''
        return subprocess.run(["bash", "-c", command, "_", str(HELPER), str(self.workspace)],
                              env=self.env, text=True, capture_output=True)

    def test_selected_modules_ignore_cache_symlink_and_ambient_list(self):
        result = self.run_selection()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout), {
            "ZEPHYR_BASE": str(self.workspace / "zephyr"),
            "Zephyr_DIR": str(self.workspace / "zephyr/share/zephyr-package/cmake"),
            "ZEPHYR_MODULES": ";".join(map(str, self.modules))})

    def test_failed_project_discovery_cannot_fall_back_to_ambient(self):
        self.env["TEST_WEST_EXIT"] = "1"
        result = self.run_selection()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("project discovery failed", result.stderr)

    def test_empty_selected_modules_cannot_fall_back_to_ambient(self):
        self.env["TEST_PROJECTS"] = ""
        result = self.run_selection()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no Zephyr modules", result.stderr)

if __name__ == "__main__":
    unittest.main()
