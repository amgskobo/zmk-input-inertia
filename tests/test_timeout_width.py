# SPDX-License-Identifier: MIT
"""Actual production timeout arithmetic, both kernel tick ABIs and controls."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


class TimeoutWidth(unittest.TestCase):
    def test_exact_conversion_width_and_identified_faults(self):
        owner = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='inertia-timeout-') as folder:
            root = Path(folder)
            source = root / 'actual.c'
            subprocess.run(['python3', str(owner / 'tests/runtime/run.py'), '--timeout', str(source)],
                           check=True, capture_output=True, text=True)
            original = source.read_text()
            controls = {
                'floor-instead-of-ceil': ('return ((uint32_t)ms * hz + 999U) / 1000U;',
                                         'return ((uint32_t)ms * hz) / 1000U;'),
                'unchecked-product': ('if (hz <= (UINT32_MAX - 999U) / UINT16_MAX)', 'if (1)'),
                'lost-fraction': ('(remainder * (hz % 1000U) + 999U) / 1000U', '0U'),
                'wide-division': ('if (hz <= (UINT32_MAX - 999U) / UINT16_MAX) {\n'
                                  '        return ((uint32_t)ms * hz + 999U) / 1000U;\n    }',
                                  'return (k_ticks_t)(((uint64_t)ms * hz + 999U) / 1000U);'),
            }
            for ticks in ('64', '32'):
                abi_flags = ['-DINERTIA_TIMEOUT_32=1'] if ticks == '32' else []
                for name in ('original', *controls):
                    with self.subTest(ticks=ticks, control=name):
                        text = original
                        if name != 'original':
                            before, after = controls[name]
                            self.assertEqual(text.count(before), 1, before)
                            text = text.replace(before, after)
                        unit = root / (name + ticks + '.c')
                        obj, binary = unit.with_suffix('.o'), root / (name + ticks)
                        unit.write_text(text)
                        subprocess.run(['cc', '-m32', '-O2', '-std=c11', *abi_flags,
                                        '-DINERTIA_TIMEOUT_WIDTH_PROBE=1', '-c', str(unit), '-o', str(obj)],
                                       check=True, capture_output=True, text=True)
                        symbols = subprocess.check_output(['nm', '--undefined-only', str(obj)], text=True)
                        found = re.findall(r'__(?:u?divdi3|u?moddi3|u?divmoddi4|muldi3)', symbols)
                        if name == 'wide-division':
                            self.assertTrue(found, symbols)
                            print(f'ticks{ticks} compiled wide control rejected: {found}')
                        elif name == 'original':
                            self.assertEqual(found, [], symbols)
                        subprocess.run(['cc', '-m32', '-O2', '-std=c11', *abi_flags,
                                        str(unit), '-o', str(binary)], check=True,
                                       capture_output=True, text=True)
                        for repeat in range(3):
                            result = subprocess.run([str(binary)], capture_output=True, text=True)
                            if name in ('original', 'wide-division'):
                                self.assertEqual(result.returncode, 0, result.stderr)
                            else:
                                self.assertEqual(result.returncode, -6, result.stderr)
                                self.assertRegex(result.stderr, r'[^\n]+:[0-9]+: [^\n]+: Assertion .+ failed\.')
                                print(f'ticks{ticks} {name} repeat{repeat + 1}: {result.stderr.strip()}')


if __name__ == '__main__':
    unittest.main()
