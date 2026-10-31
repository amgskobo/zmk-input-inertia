# SPDX-License-Identifier: MIT
"""Complete actual scalar adapter, distinct from kernel/driver function fixtures."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
def check_report(text, production):
    expected=set(re.findall(r'^static[^;{}]+?\b(\w+)\([^;{}]*\)\s*\{', production, re.M))
    functions={name:int(count) for name,count in re.findall(r'^function (\w+) called (\d+)',text,re.M)}
    executable=re.findall(r'^[ \t]*([^:\r\n]+):[ \t]*([1-9]\d*):',text,re.M)
    measured=[count.strip() for count,_ in executable if count.strip()!='-']
    if not measured:
        raise RuntimeError('coverage report has no executable lines')
    if not expected or not expected.issubset(functions) or any(functions[n]==0 for n in expected):
        raise RuntimeError('coverage report has no executable lines for a gated function')
    branches=re.findall(r'^branch.*',text,re.M)
    if any(c.startswith(('#####','=====')) for c in measured) or not branches or any(
            re.search(r'taken 0(?: |$)|never executed',b) for b in branches):
        raise RuntimeError('production line/branch was not executed')
    return len(expected),len(measured),len(branches)


def main():
    mode=os.environ.get('ZMK_TEST_MODE','quick')
    if mode not in ('quick','full'):raise SystemExit('invalid test mode')
    source=root/'src/input_processor_inertia_custom_settings.c'
    production=source.read_text(encoding='utf-8')
    for report in ('', 'metadata only', 'function read_int called 0\n 1:1:text'):
        try:check_report(report,production)
        except RuntimeError:pass
        else:raise RuntimeError('adapter coverage accepted absent/unmeasured functions')
    variants=[('sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]
    if mode=='full':variants += [('optimized',['-O2']),('coverage',['-O0','--coverage'])]
    with tempfile.TemporaryDirectory(prefix='inertia-settings-') as folder:
        work=Path(folder)
        for label,flags in variants:
            binary=work/label
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',*flags,
                '-I'+str(root/'tests/support'),'-I'+str(root/'include'),
                str(root/'tests/runtime/custom_settings_harness.c'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
        if mode=='full':
            subprocess.run(['gcov','-b','-c',str(work/'coverage-custom_settings_harness.gcno')],cwd=work,check=True,capture_output=True)
            report=(work/(source.name+'.gcov')).read_text(encoding='utf-8')
            try:counts=check_report(report,production)
            except RuntimeError:
                print(report);raise
            print('settings complete functions/lines/branches',counts)
        for macro,message,compile_failure in (
                ('TEST_IGNORE_APPLY','Inertia settings apply',False),
                ('TEST_LONG_STORAGE','Inertia storage key too long',True),
                ('TEST_LONG_KEY','Inertia setting key too long',True)):
            result=subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-D'+macro,
                '-I'+str(root/'tests/support'),'-I'+str(root/'include'),
                str(root/'tests/runtime/custom_settings_harness.c'),'-o',str(work/'control')],capture_output=True,text=True)
            if not compile_failure:
                if result.returncode:raise RuntimeError(result.stderr)
                result=subprocess.run([str(work/'control')],capture_output=True,text=True)
            if result.returncode==0 or message not in result.stderr:raise RuntimeError('missing negative control: '+message)
            print('negative control:',macro,'detected',message)
if __name__=='__main__':main()
