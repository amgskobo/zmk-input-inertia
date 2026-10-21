# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
"""Drive the real runtime entry with deliberately incomplete tool output."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
import unittest


class RuntimeCoverageAdmission(unittest.TestCase):
    def test_unmeasured_runtime_cannot_pass(self):
        root = Path(__file__).resolve().parents[1]
        for kind in ('empty', 'outside', 'missing-range'):
            with self.subTest(report=kind), tempfile.TemporaryDirectory() as folder:
                work = Path(folder)
                tools = work / 'bin'
                tools.mkdir()
                compiler = tools / 'cc'
                compiler.write_text(textwrap.dedent('''\
                    #!/usr/bin/env python3
                    import json, os, pathlib, sys
                    args = sys.argv[1:]
                    with open(os.environ["RECORDING_COMPILER_LOG"], "a") as log:
                        log.write(json.dumps(args) + "\\n")
                    output = pathlib.Path(args[args.index("-o") + 1])
                    output.write_text("#!/bin/sh\\nexit 0\\n")
                    output.chmod(0o755)
                    '''), encoding='utf-8')
                compiler.chmod(0o755)
                gcov = tools / 'gcov'
                gcov.write_text(textwrap.dedent('''\
                    #!/usr/bin/env python3
                    import os, pathlib
                    kind = os.environ["COVERAGE_REPORT_KIND"]
                    text = "" if kind == "empty" else "1:0:unrelated metadata\\n"
                    sources = pathlib.Path(os.environ["CONTROLLED_OWNER_ROOT"], "src")
                    for source in sources.glob("*.c"):
                        report = text
                        if kind == "missing-range":
                            lines = source.read_text().splitlines()
                            # One real executable function line is still insufficient.
                            for number, line in enumerate(lines, 1):
                                if 'if (' in line:
                                    report = "1:" + str(number) + ":one function only\\n"
                                    break
                        pathlib.Path(source.name + ".gcov").write_text(report)
                    '''), encoding='utf-8')
                gcov.chmod(0o755)
                env = dict(os.environ, ZMK_TEST_MODE='full', PYTHONDONTWRITEBYTECODE='1',
                           RECORDING_COMPILER_LOG=str(work / 'compiler.jsonl'),
                           COVERAGE_REPORT_KIND=kind, CONTROLLED_OWNER_ROOT=str(root))
                env['PATH'] = str(tools) + os.pathsep + env['PATH']
                result = subprocess.run([sys.executable, str(root / 'tests/runtime/run.py')],
                                        env=env, capture_output=True, text=True, timeout=30)
                self.assertNotEqual(result.returncode, 0,
                                    'unmeasured runtime falsely passed: ' + result.stdout)
                self.assertIn('coverage report has no executable lines', result.stderr)
                if kind == 'missing-range':
                    self.assertIn('a gated function range', result.stderr)
                calls = (work / 'compiler.jsonl').read_text(encoding='utf-8').splitlines()
                self.assertGreater(len(calls), 0)
                self.assertTrue(all(isinstance(json.loads(row), list) for row in calls))


if __name__ == '__main__':
    unittest.main()
