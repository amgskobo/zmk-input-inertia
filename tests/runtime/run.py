"""Measure actual stream and frame-routing helpers against host stubs."""

import pathlib
import re
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
source_path = root / "src/input_processor_inertia.c"
source = source_path.read_text(encoding="utf-8")
names = (
    "inertia_abs_i16",
    "ctrl_mod_is_active",
    "clear_motion_state",
    "motion_has_state",
    "cancel_move_locked",
    "cancel_scroll_locked",
    "stream_for_event",
    "begin_move_frame_locked",
    "begin_scroll_frame_locked",
)
functions = []
for name in names:
    found = None
    for match in re.finditer(r"\b" + name + r"\s*\(", source):
        start = source.rfind("\nstatic", 0, match.start()) + 1
        preceding = source[start:match.start()]
        if start and ";" not in preceding and "}" not in preceding:
            found = start
            break
    if found is None:
        raise RuntimeError(f"missing driver definition: {name}")
    end = source.index("\n}", match.end()) + 2
    line = source.count("\n", 0, found) + 1
    functions.append(f'#line {line} "{source_path}"\n' + source[found:end])

harness = (root / "tests/runtime/harness.c").read_text(encoding="utf-8")
with tempfile.TemporaryDirectory(prefix="inertia-runtime-") as folder:
    work = pathlib.Path(folder)
    unit = work / "test.c"
    unit.write_text(harness.replace("/* DRIVER_FUNCTIONS */", "\n".join(functions) +
                                    f'\n#line 1 "{unit}"\n'), encoding="utf-8")
    for variant, flags in (
        ("optimized", ["-O2"]),
        ("sanitized", ["-O1", "-g", "-fno-omit-frame-pointer",
                       "-fsanitize=address,undefined", "-fno-sanitize-recover=all"]),
        ("coverage", ["-O0", "--coverage"]),
    ):
        binary = work / variant
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", *flags,
                        "-I" + str(root / "include"), str(unit),
                        str(root / "src/inertia_core.c"), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    report = subprocess.run(["gcov", "-b", "-c", "-o", str(work / "coverage-test.gcno"),
                             str(unit)], cwd=work, check=True, capture_output=True, text=True)
    print(report.stdout, end="")
    driver = report.stdout.split(f"File '{source_path}'", 1)[1].split("Creating ", 1)[0]
    if "Lines executed:100.00%" not in driver or "Taken at least once:100.00%" not in driver:
        raise RuntimeError("extracted stream/frame helpers fell below 100% coverage")
