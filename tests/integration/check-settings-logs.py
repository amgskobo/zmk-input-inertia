# SPDX-License-Identifier: MIT
"""Require all first-boot and reload phases, so an early reload exit cannot pass."""
from pathlib import Path
import sys
FIRST = 'PASS: Inertia registry range/type rejection, per-node apply and NVS save'
RELOAD = 'PASS: persisted Inertia value reloads'
def validate(first, reload):
    assert FIRST in first, "first boot did not complete settings save"
    assert RELOAD in reload, "reload did not verify persisted driver state"
def main():
    if len(sys.argv)!=3:raise SystemExit('pass first and reload filename patterns with {i}')
    for i in (1,2,3):
        first=Path(sys.argv[1].format(i=i)).read_text()
        reload=Path(sys.argv[2].format(i=i)).read_text()
        validate(first,reload)
        for a,b in ((first.replace(FIRST,''),reload),(first,reload.replace(RELOAD,''))):
            try:validate(a,b)
            except AssertionError:pass
            else:raise AssertionError('incomplete boot control was not detected')
    print('PASS: three complete first-boot/reload pairs and incomplete-phase controls')
if __name__=='__main__':main()
