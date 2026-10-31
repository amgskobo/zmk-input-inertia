# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
import os
import itertools
from pathlib import Path
import subprocess
import tempfile
import unittest

ENTRIES = ['tests/endpoint-boundary/run.sh']

class FreshOutput(unittest.TestCase):
    def test_existing_output_rejected_before_west_or_any_write(self):
        root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as folder:
            temp = Path(folder)
            output = temp / "existing-evidence"
            output.mkdir()
            sentinel = output / "sentinel.txt"
            sentinel.write_text("preserve prior ordered evidence\n")
            linked = temp / "linked-evidence"
            linked.symlink_to(output, target_is_directory=True)
            dangling = temp / "dangling-evidence"
            dangling.symlink_to(temp / "absent", target_is_directory=True)
            stub = temp / "bin"
            stub.mkdir()
            marker = temp / "west-was-called"
            west = stub / "west"
            west.write_text('#!/bin/sh\necho called >> "$WEST_MARKER"\nexit 7\n')
            west.chmod(0o755)
            env = dict(os.environ, PATH=str(stub) + os.pathsep + os.environ["PATH"],
                       WEST_MARKER=str(marker), ZMK_TEST_OUTPUT=str(output),
                       INERTIA_NATIVE_ARTIFACTS=str(output))
            for entry, candidate in itertools.product(ENTRIES, (output, linked, dangling)):
                with self.subTest(entry=entry, output=str(candidate)):
                    env["ZMK_TEST_OUTPUT"] = str(candidate)
                    env["INERTIA_NATIVE_ARTIFACTS"] = str(candidate)
                    command = ["bash", str(root / entry)]
                    if "endpoint-boundary" not in entry:
                        command.append(str(temp))
                    result = subprocess.run(command, env=env, text=True,
                                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                            timeout=5, check=False)
                    self.assertEqual(result.returncode, 2, result.stdout)
                    self.assertIn("output already exists", result.stdout)
                    self.assertFalse(marker.exists(), result.stdout)
                    self.assertEqual(sentinel.read_text(), "preserve prior ordered evidence\n")

if __name__ == "__main__":
    unittest.main()
