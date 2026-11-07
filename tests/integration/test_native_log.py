# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
"""Native admission controls use real recovering sanitizer ELF executables.

Only west's workspace/build responses are controlled. The actual owner launchers,
timeout, ELF execution, exit/marker checks and log validator remain in the path.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest

from native_log import validate

ROOT = Path(__file__).resolve().parents[2]
ENTRY_ROOT = ROOT
CONTROL_ELF_ROOT = None

WEST = '''#!/usr/bin/env python3
import os, pathlib, shutil, sys
args = sys.argv[1:]
workspace = pathlib.Path(os.environ['CONTROL_WORKSPACE'])
if args[0] == 'topdir':
    print(workspace)
elif args[0] == 'list':
    print(workspace / ('zephyr' if args[1] == 'zephyr' else 'zmk'))
elif args[0] == 'build':
    config = next((arg.split('=', 1)[1] for arg in args if arg.startswith('-DZMK_CONFIG=')), '')
    if '/guards/' in config:
        print((pathlib.Path(config) / 'expected-errors.txt').read_text())
        sys.exit(1)
    build = pathlib.Path(args[args.index('-d') + 1])
    (build / 'zephyr').mkdir(parents=True, exist_ok=True)
    runtime = build.name == 'runtime'
    source = os.environ['CONTROL_ELF']
    if os.environ['CONTROL_ENTRY'] == 'integration' and not runtime:
        source = os.environ['CONTROL_CLEAN_ELF']
    shutil.copyfile(source, build / 'zephyr/zmk.exe')
    (build / 'zephyr/zmk.exe').chmod(0o755)
    for name in ('CMakeCache.txt', 'zephyr/zephyr.dts'):
        (build / name).write_text('admission control, not a Zephyr build\\n')
    (build / 'zephyr/.config').write_text('CONFIG_ZMK_INPUT_INERTIA_CUSTOM_SETTINGS=y\\n')
'''


class DiagnosticSyntax(unittest.TestCase):
    def test_source_located_ubsan_and_sanitizer_reports_rejected(self):
        for line in (
            '/selected/core/kscan_mock.c:103:29: runtime error: left shift overflow',
            'relative source.c:2: runtime error: invalid load',
            r'C:\source\owner.cpp:42:7: runtime error: invalid shift',
            '==123==ERROR: AddressSanitizer: stack-buffer-overflow',
            'SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior owner.c:2:3',
            'AddressSanitizer:DEADLYSIGNAL',
            'WARNING: ThreadSanitizer: data race',
            'SUMMARY: MemorySanitizer: use-of-uninitialized-value',
            'ERROR: LeakSanitizer: detected memory leaks',
        ):
            with self.subTest(line=line), self.assertRaisesRegex(ValueError, 'native sanitizer diagnostic'):
                validate('before\n' + line + '\nafter\n')

    def test_benign_locationless_labels_accepted(self):
        validate('runtime error: simulated endpoint refusal\n'
                 'module: runtime error: reported endpoint label\n'
                 'documentation mentions AddressSanitizer without a finding\n')


class ActualLauncherAdmission(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.base = Path(cls.temp.name)
        cls.addClassCleanup(cls.temp.cleanup)
        cls.elfs = {}
        cls.compile_records = []
        for kind, flags in (
            ('clean', []),
            ('ubsan', ['-DCONTROL_UBSAN', '-fsanitize=undefined', '-fsanitize-recover=all']),
            ('asan', ['-DCONTROL_ASAN', '-fsanitize=address', '-fsanitize-recover=address']),
        ):
            elf = cls.base / kind
            command = ['cc', '-std=c11', '-O1', '-g', '-fno-omit-frame-pointer',
                       '-fno-pie', '-no-pie', *flags,
                       str(ROOT / 'tests/integration/sanitizer_control.c'), '-o', str(elf)]
            started = datetime.datetime.now(datetime.timezone.utc).isoformat()
            before = time.monotonic()
            if CONTROL_ELF_ROOT:
                if (CONTROL_ELF_ROOT / 'sanitizer_control.c').read_bytes() != (ROOT / 'tests/integration/sanitizer_control.c').read_bytes():
                    raise ValueError('preserved control source differs')
                shutil.copyfile(CONTROL_ELF_ROOT / (kind + '.elf'), elf)
                elf.chmod(0o755)
                cls.compile_records.append({'kind': kind, 'reused_exact_elf': str(CONTROL_ELF_ROOT / (kind + '.elf'))})
            else:
                result = subprocess.run(command, check=True, capture_output=True, text=True)
                cls.compile_records.append({'kind': kind, 'argv': command, 'exit': result.returncode,
                                            'started_utc': started, 'elapsed_seconds': time.monotonic() - before,
                                            'stdout': result.stdout, 'stderr': result.stderr})
            cls.elfs[kind] = elf
        cls.stub = cls.base / 'bin'
        cls.stub.mkdir()
        (cls.stub / 'west').write_text(WEST)
        (cls.stub / 'west').chmod(0o755)
        cls.workspace = cls.base / 'workspace'
        (cls.workspace / 'zephyr').mkdir(parents=True)
        (cls.workspace / 'zmk/zephyr').mkdir(parents=True)
        (cls.workspace / 'zmk/zephyr/module.yml').write_text('name: admission-control\n')
        retain = os.environ.get('INERTIA_ADMISSION_EVIDENCE')
        cls.evidence = Path(retain) if retain else None
        if cls.evidence:
            if not cls.evidence.is_absolute() or cls.evidence.exists() or cls.evidence.is_symlink():
                raise ValueError('admission evidence must be fresh and absolute')
            cls.evidence.mkdir(parents=True)
            shutil.copyfile(ROOT / 'tests/integration/sanitizer_control.c', cls.evidence / 'sanitizer_control.c')
            for kind, elf in cls.elfs.items():
                shutil.copyfile(elf, cls.evidence / (kind + '.elf'))
            (cls.evidence / 'compile.json').write_text(json.dumps(cls.compile_records, indent=2) + '\n')
            (cls.evidence / 'elf-identities.json').write_text(json.dumps({
                kind: hashlib.sha256(elf.read_bytes()).hexdigest() for kind, elf in cls.elfs.items()
            }, indent=2) + '\n')
        cls.sequence = 0

    def environment(self, kind, phase):
        env = dict(os.environ, PATH=str(self.stub) + os.pathsep + os.environ['PATH'],
                   CONTROL_WORKSPACE=str(self.workspace), CONTROL_ELF=str(self.elfs[kind]),
                   CONTROL_CLEAN_ELF=str(self.elfs['clean']), INERTIA_CONTROL_PHASE=phase,
                   ASAN_OPTIONS='halt_on_error=0:detect_leaks=0',
                   UBSAN_OPTIONS='halt_on_error=0:print_stacktrace=1',
                   PYTHONDONTWRITEBYTECODE='1')
        for key in ('ZMK_TEST_OFFLINE_WORKSPACE', 'ZMK_TEST_OUTPUT', 'INERTIA_NATIVE_ARTIFACTS',
                    'INERTIA_NATIVE_MODE', 'INERTIA_LIFECYCLE_CASE'):
            env.pop(key, None)
        return env

    def execute(self, entry, kind, phase='both'):
        type(self).sequence += 1
        label = f'{type(self).sequence:03d}-{entry}-{kind}-{phase}'
        output = self.base / label
        env = self.environment(kind, phase)
        env['CONTROL_ENTRY'] = entry
        if entry == 'integration':
            env['ZMK_TEST_OUTPUT'] = str(output)
            command = ['bash', str(ENTRY_ROOT / 'tests/integration/run.sh'), 'dya']
        elif entry == 'endpoint':
            env['INERTIA_NATIVE_ARTIFACTS'] = str(output)
            command = ['bash', str(ENTRY_ROOT / 'tests/endpoint-boundary/run.sh')]
        else:
            command = ['bash', str(ENTRY_ROOT / 'tests/review-shared-hid/run.sh'),
                       str(self.workspace), str(output)]
        started = datetime.datetime.now(datetime.timezone.utc).isoformat()
        before = time.monotonic()
        result = subprocess.run(command, cwd=self.workspace, env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        if self.evidence:
            record = self.evidence / label
            record.mkdir()
            (record / 'launcher.log').write_text(result.stdout)
            (record / 'command.json').write_text(json.dumps({'argv': command, 'exit': result.returncode,
                                                             'kind': kind, 'phase': phase,
                                                             'started_utc': started, 'elapsed_seconds': time.monotonic() - before,
                                                             'ASAN_OPTIONS': env['ASAN_OPTIONS'],
                                                             'UBSAN_OPTIONS': env['UBSAN_OPTIONS']}, indent=2) + '\n')
            if output.exists():
                shutil.copytree(output, record / 'output', symlinks=True)
        return result, output

    def test_actual_elf_recovers_with_markers_and_zero_exit(self):
        for kind in ('ubsan', 'asan'):
            for repeat in range(3):
                with self.subTest(kind=kind, repeat=repeat):
                    result = subprocess.run([str(self.elfs[kind])], env=self.environment(kind, 'both'),
                                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=5)
                    self.assertEqual(result.returncode, 0, result.stdout)
                    self.assertIn('inertia runtime tests: PASS', result.stdout)
                    with self.assertRaisesRegex(ValueError, 'native sanitizer diagnostic'):
                        validate(result.stdout)
                    if self.evidence:
                        (self.evidence / f'raw-{kind}-{repeat + 1}.log').write_text(result.stdout)

    def test_actual_launchers_reject_recovering_elf_three_fresh_starts(self):
        for entry, phase in (('integration', 'first'), ('integration', 'reload'),
                             ('endpoint', 'both'), ('shared', 'both')):
            for kind in ('ubsan', 'asan'):
                for repeat in range(3):
                    with self.subTest(entry=entry, phase=phase, kind=kind, repeat=repeat):
                        result, output = self.execute(entry, kind, phase)
                        self.assertNotEqual(result.returncode, 0, result.stdout)
                        self.assertIn('native sanitizer diagnostic', result.stdout)
                        if entry == 'integration':
                            exit_file = ('build/runtime.run-1.exit' if phase == 'first'
                                         else 'build/runtime/reload-1.exit')
                            self.assertEqual((output / exit_file).read_text().strip(), '0')
                        else:
                            self.assertEqual((output / 'run-1.exit').read_text().strip(), '0')

    def test_actual_launchers_accept_benign_locationless_label(self):
        for entry in ('integration', 'endpoint', 'shared'):
            with self.subTest(entry=entry):
                result, _ = self.execute(entry, 'clean')
                self.assertEqual(result.returncode, 0, result.stdout)

    def test_settings_validator_rejects_either_diagnosed_phase(self):
        for kind in ('ubsan', 'asan'):
            diagnosed = subprocess.run([str(self.elfs[kind])], env=self.environment(kind, 'both'),
                                      text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                      timeout=5, check=True).stdout
            benign = subprocess.run([str(self.elfs['clean'])], env=self.environment('clean', 'both'),
                                    text=True, capture_output=True, timeout=5, check=True).stdout
            for phase in ('first', 'reload'):
                folder = self.base / f'settings-{kind}-{phase}'
                folder.mkdir()
                for repeat in (1, 2, 3):
                    (folder / f'first-{repeat}.log').write_text(diagnosed if phase == 'first' else benign)
                    (folder / f'reload-{repeat}.log').write_text(diagnosed if phase == 'reload' else benign)
                result = subprocess.run(['python3', str(ENTRY_ROOT / 'tests/integration/check-settings-logs.py'),
                                         str(folder / 'first-{i}.log'), str(folder / 'reload-{i}.log')],
                                        text=True, capture_output=True, timeout=5)
                with self.subTest(kind=kind, phase=phase):
                    self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn('native sanitizer diagnostic', result.stdout + result.stderr)
                if self.evidence:
                    shutil.copytree(folder, self.evidence / folder.name)
                    (self.evidence / (folder.name + '.json')).write_text(json.dumps({
                        'exit': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr}, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--entry-root', type=Path, default=ROOT)
    parser.add_argument('--control-elfs', type=Path)
    options, remainder = parser.parse_known_args()
    ENTRY_ROOT = options.entry_root.resolve()
    CONTROL_ELF_ROOT = options.control_elfs.resolve() if options.control_elfs else None
    unittest.main(argv=[__file__, *remainder])
