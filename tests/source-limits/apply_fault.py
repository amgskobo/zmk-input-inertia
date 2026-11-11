#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Remove one route check only in a distinct disposable owner copy."""
from pathlib import Path
import sys

owner = Path(__file__).resolve().parents[2]
fault, target = sys.argv[1:]
target = Path(target).resolve()
if target == owner or owner in target.parents or target in owner.parents:
    raise SystemExit('fault target must be a distinct disposable owner copy')
source = target / 'src/input_processor_inertia.c'
text = source.read_text()
needle = {
    'base': 'INERTIA_CHECK_ROUTE(DT_INST(source, compat), source)',
    'child': 'DT_FOREACH_CHILD_VARGS(DT_INST(source, compat), INERTIA_CHECK_ROUTE, source)',
    'device': 'DT_NODE_HAS_STATUS(DT_PHANDLE(DT_INST(source, compat), device), okay)',
}[fault]
if text.count(needle) != 1:
    raise SystemExit('expected exactly one original route check')
after = '1' if fault == 'device' else '/* identified missing-' + fault + '-guard control */'
source.write_text(text.replace(needle, after))
