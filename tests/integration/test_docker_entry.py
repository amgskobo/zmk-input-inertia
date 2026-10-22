# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class DockerHandoff(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        stub = self.base / 'bin'
        stub.mkdir()
        self.calls = self.base / 'docker-args.json'
        docker = stub / 'docker'
        docker.write_text("#!/usr/bin/env python3\nimport json,os,pathlib,sys\npathlib.Path(os.environ['DOCKER_CALLS']).write_text(json.dumps(sys.argv[1:]))\nsys.exit(int(os.environ.get('DOCKER_EXIT','0')))\n")
        docker.chmod(0o755)
        west = stub / 'west'
        west.write_text('#!/bin/sh\necho called >> "$WEST_CALLS"\nexit 86\n')
        west.chmod(0o755)
        self.west_calls = self.base / 'west-calls'
        self.env = dict(os.environ, PATH=str(stub) + os.pathsep + os.environ['PATH'],
                        DOCKER_CALLS=str(self.calls), WEST_CALLS=str(self.west_calls),
                        TMPDIR=str(self.base), PYTHONDONTWRITEBYTECODE='1')
        for key in ('ZMK_TEST_OUTPUT', 'ZMK_TEST_OFFLINE_WORKSPACE',
                    'ZMK_TEST_WORKSPACE', 'ZMK_TEST_WORKSPACE_VOLUME', 'DOCKER_EXIT'):
            self.env.pop(key, None)

    def run_entry(self, variant='upstream', entry='tests/run-integration-docker.sh', **env):
        return subprocess.run(['bash', str(ROOT / entry), variant],
                              env=dict(self.env, **env), capture_output=True, text=True, timeout=10)

    def test_variants_readonly_source_and_fresh_results_cross_docker(self):
        for variant in ('upstream', 'dya'):
            with self.subTest(variant=variant):
                output = self.base / 'results parent' / ('fresh ' + variant)
                result = self.run_entry(variant, ZMK_TEST_OUTPUT=str(output))
                self.assertEqual(result.returncode, 0, result.stderr)
                args = json.loads(self.calls.read_text())
                self.assertIn(f'{ROOT}:/src:ro', args)
                self.assertIn(f'{output.parent}:/results', args)
                self.assertIn('ZMK_TEST_OUTPUT=/results/' + output.name, args)
                self.assertEqual(args[-3:], ['/bin/bash', '/src/tests/integration/run.sh', variant])
                self.assertFalse(output.exists())

    def test_unknown_variant_fails_before_any_tool_or_output(self):
        for entry in ('tests/run-integration-docker.sh', 'tests/integration/run.sh'):
            with self.subTest(entry=entry):
                output = self.base / 'must-not-exist'
                result = self.run_entry('unknown', entry, ZMK_TEST_OUTPUT=str(output))
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertFalse(output.exists())
                self.assertFalse(self.calls.exists())
                self.assertFalse(self.west_calls.exists())

    def test_existing_linked_dangling_and_relative_results_are_preserved(self):
        output = self.base / 'prior-results'
        output.mkdir()
        sentinel = output / 'sentinel'
        sentinel.write_text('preserve prior ordered evidence\n')
        linked = self.base / 'linked'
        linked.symlink_to(output, target_is_directory=True)
        dangling = self.base / 'dangling'
        dangling.symlink_to(self.base / 'absent', target_is_directory=True)
        for entry in ('tests/run-integration-docker.sh', 'tests/integration/run.sh'):
            for candidate in (output, linked, dangling, Path('relative-results')):
                with self.subTest(entry=entry, candidate=str(candidate)):
                    result = self.run_entry(entry=entry, ZMK_TEST_OUTPUT=str(candidate))
                    self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                    self.assertIn('fresh absolute path', result.stderr)
                    self.assertFalse(self.calls.exists())
                    self.assertFalse(self.west_calls.exists())
                    self.assertEqual(sentinel.read_text(), 'preserve prior ordered evidence\n')

    def test_online_failure_retains_selected_manifest_and_requested_output(self):
        for variant in ('upstream', 'dya'):
            with self.subTest(variant=variant):
                output = self.base / ('retained-' + variant)
                result = self.run_entry(variant, 'tests/integration/run.sh',
                                        ZMK_TEST_OUTPUT=str(output))
                self.assertEqual(result.returncode, 86, result.stdout + result.stderr)
                self.assertEqual((output / 'workspace/config/west.yml').read_bytes(),
                                 (ROOT / f'tests/integration/{variant}/west.yml').read_bytes())
                self.assertTrue(self.west_calls.exists())

    def test_child_failure_is_propagated(self):
        result = self.run_entry('dya', DOCKER_EXIT='37')
        self.assertEqual(result.returncode, 37, result.stderr)

    def test_online_manifest_freezes_only_projects_selected_by_update(self):
        west = Path(self.env['PATH'].split(os.pathsep)[0]) / 'west'
        west.write_text(
            '#!/usr/bin/env python3\n'
            'import json, os, pathlib, sys\n'
            'args = sys.argv[1:]\n'
            'with pathlib.Path(os.environ["WEST_CALLS"]).open("a") as log:\n'
            '    log.write(json.dumps(args) + "\\n")\n'
            'if args and args[0] == "manifest":\n'
            '    sys.exit(86 if args == ["manifest", "--freeze", "--active-only"] else 87)\n'
        )
        for variant in ('upstream', 'dya'):
            with self.subTest(variant=variant):
                output = self.base / ('active-manifest-' + variant)
                result = self.run_entry(variant, 'tests/integration/run.sh',
                                        ZMK_TEST_OUTPUT=str(output))
                self.assertEqual(result.returncode, 86, result.stdout + result.stderr)
                calls = [json.loads(line) for line in self.west_calls.read_text().splitlines()]
                self.assertIn(['update', '--narrow', '--fetch-opt=--depth=1'], calls)
                self.assertEqual(calls[-1], ['manifest', '--freeze', '--active-only'])


if __name__ == '__main__':
    unittest.main()
