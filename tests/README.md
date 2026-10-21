# Module-owned tests

Run from this checkout. Docker and Bash are the host entry point; no keyboard
checkout or simulator repository supplies assertions.

```bash
bash tests/run-docker.sh
ZMK_TEST_MODE=full bash tests/run-docker.sh
```

Quick runs every behavioral scenario once under ASan/UBSan, plus infrastructure
unit tests. Full retains the same scenarios with optimized, coverage and 32-bit
variants. Full requires 100% line/branch coverage of the core and lifted driver
functions. Unknown modes fail. PowerShell users can set `$env:ZMK_TEST_MODE="full"`
and invoke Git Bash explicitly; remove the variable to restore quick mode.

## Behavioral boundaries

| Fixture | Assertions |
| --- | --- |
| [test_inertia_core.c](test_inertia_core.c) | Fixed-point arithmetic, frame qualification and decay boundaries |
| [runtime/helpers_harness.c](runtime/helpers_harness.c) | Qualification, independent state, decay and arithmetic helpers |
| [runtime/driver_harness.c](runtime/driver_harness.c) | Scheduling, first-axis cancellation, generation checks, bounded pending frames and queue recovery |
| [runtime/test_hook_harness.c](runtime/test_hook_harness.c) | Guarded production probes |
| [test_runtime_coverage.py](test_runtime_coverage.py) | Actual full runtime entry rejects empty, unrelated and partially measured coverage |

Host kernel and endpoint calls are controlled stand-ins. Preserve normal,
boundary/invalid, independent-state, cancellation and failure/recovery assertions;
coverage totals do not establish complete scheduling or transport acceptance.
[contract.json](contract.json) and [maintenance.json](maintenance.json) map current
commands, assertions and gaps.

Both host modes and CI run the coverage-admission controls with recording
compiler/gcov tools. Each gated real function needs executable lines; macro
expansions remain measured in their callers. Normal full runs retain actual
compiled line/branch measurement and all behavioral assertions. The synthetic
controls prove gate behavior, not firmware execution.

## Both supported cores and retained CI results

```bash
ZMK_TEST_OUTPUT=/absolute/fresh-upstream bash tests/run-integration-docker.sh upstream
ZMK_TEST_OUTPUT=/absolute/fresh-dya bash tests/run-integration-docker.sh dya
```

The default variant is `upstream`. Each online run creates a new west workspace
from the selected owner manifest, resolves stock `main` or DYA `main+dya`, and
records its frozen manifest. These online branches move; they are not the fixed
offline pins below. CI runs both variants, uses a digest-pinned build image,
and retains logs, identities, config, DTS and native binaries even after failure.
Push/PR uses quick host checks; scheduled/manual runs use full host checks.

`ZMK_TEST_OUTPUT` is optional on the Docker entry. An explicit output must be a
fresh absolute host path and is retained on success or failure. Existing paths,
directory links and dangling links fail before Docker or west is invoked.
Without an explicit output, the online entry removes its temporary workspace.
The module source is mounted read-only; there is no mutable workspace-reuse
mode. [integration/test_docker_entry.py](integration/test_docker_entry.py) checks
both variants, readonly mounting, evidence preservation and child exit status.

The integration entry builds the smoke/runtime fixtures, runs the endpoint and
shared-HID boundaries below, and requires all 13 invalid-DTS/backend guards to
fail for their named reason. Synchronous input is rejected by the selected
ZMK pointing Kconfig before the defensive driver assertion; expected warnings
are preserved.

## Real kernel and input-thread boundaries

The canonical integration entry includes both focused commands. In an already
resolved west workspace, they can also run separately without fetching:

```bash
INERTIA_NATIVE_ARTIFACTS=/absolute/fresh-endpoint bash /module/tests/endpoint-boundary/run.sh
bash /module/tests/review-shared-hid/run.sh /resolved/workspace /absolute/fresh-hid
```

Eight endpoint groups exercise the production driver, real kernel work/mutexes,
threaded input and actual ZMK HID storage. They cover move/scroll pre-emit
cancellation, cancellation during blocked submission, another pointer's first
axis before sync, an independent scroll node and full-queue recovery without a
burst. A controlled endpoint holds an already-accepted report for 120 simulated
ms. Concurrent direct-handler cancellation must finish within one simulated ms;
no further report may be accepted. This bound starts at handler entry and does
not bound how long a sensor event waits in the input queue.

The [shared-HID fixture](review-shared-hid/README.md) uses the unmodified ZMK
pointing listener as another producer. It retains report integrity/order while
submission is held, then queues manual X, an old inertia token, and Y/SYNC in
that order. The first manual axis must invalidate the token and preserve the
exact first manual frame. Both boundaries run one binary three times.
The named negative controls prove that each regression can detect its defect;
expected reproduction is separate from product PASS.

`CONFIG_INPUT_MODE_THREAD` is required. Independent inertia nodes remain
independent; same-node streams share cancellation policy. Unrelated custom HID
writers outside the input thread are not covered. Native endpoint acceptance
does not prove physical USB/BLE delivery, queue latency, power or reconnect.

## Explicit offline integration

Inside the same Linux build image, mount the module at `/src` and select an
already resolved cache plus a fresh output:

```bash
ZMK_TEST_OFFLINE_WORKSPACE=/resolved/upstream \
ZMK_TEST_OUTPUT=/tmp/inertia-upstream-v1 \
bash /src/tests/integration/run.sh upstream
```

Use `dya` and its resolved cache for DYA. [integration/offline.py](integration/offline.py)
requires clean exact stock `5b51501fead672c41b5cfb396f3dafe0894bf4e9` or DYA
`e5c9b6915b56801193e359dd9bad4a167ce0d1b8`, Zephyr and required library pins.
It creates an unchanged local ZMK clone and dependency symlinks, preserves the
original fixture manifest and records checked identities in `dependencies.json`.
`resolved-manifest.yml` expands imports; unused uncloned projects are not required
merely to freeze that offline record. No fetch, source repair or core changes
occur. Neither Inertia fixture manifest enables custom settings.

Run infrastructure admission/output tests with:

```bash
python3 -m unittest discover -s tests/integration -p 'test_*.py' -v
```

The host entry already dispatches them. They preserve sentinels and require zero
tool calls on rejected output paths. `ZMK_TEST_OFFLINE_NEGATIVE_CONTROL=skip-identity`
deliberately bypasses identity checking and must make the rejection tests fail.
Pin changes require explicit dependency review.

Offline staging rejects output within source/dependency trees, parent-symlink
aliases, overlapping project paths and west/config metadata collisions before
creating output. External dependencies and sibling outputs remain supported.
The parseable wrong-archive fixture fails a missing-hash-rejection assertion
under the digest-bypass control; a tar error is not accepted as reproduction.

## Retired simulation and physical gaps

The active `tests/bsim` tree was removed on 2026-10-05 after SHA256-verifying an
external archive of its source and raw ordered traces. No failed snapshot was
accepted. Both retired `split-glide` and `split-wlan` retain their arithmetic,
state and error assertions in the owner fixtures above. Their exact ordered
radio/HID/LED/battery traces, physical timing, congestion/reconnect, power and
retention remain gaps requiring matching production-board evidence. The
consuming keyboard retains archive locations and the replacement map; that
archive is not a prerequisite for current module checks.
