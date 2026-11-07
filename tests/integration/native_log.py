# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
"""Reject sanitizer findings independently of native exit status and PASS markers."""
from pathlib import Path
import re
import sys


SANITIZER = re.compile(
    r"(?:\b(?:ERROR|WARNING|SUMMARY):\s*"
    r"(?:AddressSanitizer|UndefinedBehaviorSanitizer|LeakSanitizer|MemorySanitizer|ThreadSanitizer)\b"
    r"|\b(?:AddressSanitizer|UndefinedBehaviorSanitizer|MemorySanitizer):DEADLYSIGNAL\b"
    r"|^[^\r\n]+:\d+(?::\d+)?:\s*runtime error:)",
    re.MULTILINE,
)


def validate(text):
    """A source location is required for UBSan's generic 'runtime error' label."""
    match = SANITIZER.search(text)
    if match:
        line = text[text.rfind('\n', 0, match.start()) + 1:].splitlines()[0]
        raise ValueError('native sanitizer diagnostic: ' + line)


def main():
    if len(sys.argv) < 2:
        raise SystemExit('pass one or more native log files')
    for name in sys.argv[1:]:
        try:
            validate(Path(name).read_text(errors='replace'))
        except ValueError as error:
            print(f'{name}: {error}', file=sys.stderr)
            return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
