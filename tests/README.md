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
| [runtime/driver_harness.c](runtime/driver_harness.c) | Scheduling, running-handler rearm deadlines, first-axis cancellation, generation checks, bounded pending frames and queue recovery |
| [runtime/test_hook_harness.c](runtime/test_hook_harness.c) | Guarded production probes |
| [test_runtime_coverage.py](test_runtime_coverage.py) | Actual full runtime entry rejects empty, unrelated and partially measured coverage |

Host kernel and endpoint calls are controlled stand-ins. Preserve normal,
boundary/invalid, independent-state, cancellation and failure/recovery assertions;
coverage totals do not establish complete scheduling or transport acceptance.
[contract.json](contract.json) and [maintenance.json](maintenance.json) map current
commands, assertions and gaps.

## Both supported cores and retained CI results

```bash
ZMK_TEST_OUTPUT=/absolute/fresh-upstream bash tests/run-integration-docker.sh upstream
ZMK_TEST_OUTPUT=/absolute/fresh-dya bash tests/run-integration-docker.sh dya
```

The default variant is `upstream`. Each online run creates a new west workspace
from the selected owner manifest, resolves stock `main` or DYA `main+dya`, and
records a frozen manifest of all active projects selected by `west update`.
Inactive uncloned groups are not required solely for that record; the Docker
handoff regression exercises both online variants through manifest generation.
These online branches move; they are not the fixed
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

Twenty-nine endpoint groups exercise the production driver, real kernel work/mutexes,
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

### Endpoint and activity lifecycle

The same endpoint binary retains its complete `groups=29` marker and adds
`inertia lifecycle boundary: PASS groups=11`, required in all three repeats on
both supported cores. The added cases use controlled endpoint identity/readiness
as well as the existing controlled transport. Kernel work, mutexes, threaded
input, actual ZMK event dispatch and shared HID storage remain real.

Move and scroll cases cancel an armed trigger, a queued token while the real
input thread is held, and future work while an accepted endpoint report is
blocked. Cancellation must return within one simulated millisecond at handler
entry, preserve an owned slot until consumption/completion, emit no later old
report, and permit fresh input on the new peer. Two more cases change the
selected endpoint before token admission without delivering its notification;
the consumer must observe that identity change itself. No physical endpoint or
input-queue latency bound follows from this fixture.

IDLE and SLEEP cases prime the first node's movement plus stream1 scrolling on
every other DTS Inertia node. Every stream of every node must retire. Initial
and repeated endpoint/ACTIVE events preserve fresh motion, USB identity ignores
unused BLE storage, a BLE-profile change cancels even without changing transport,
and disconnect cancels existing motion. The fixture also records actual
NULL-device callback delivery of the retired MSC tokens. That establishes the
dispatcher boundary; the inspected core's unconditional activity submission
remains a core-required accounting gap. It does not claim to measure or repair
physical power or the core's activity timestamp.

Host coverage includes these lifecycle functions, no-op/unknown/not-ready input,
queued and already accepted reports, all streams, independent nodes and selected
endpoint changes at decay/admission/completion. Existing exact traces, all576
composite permutations, eight error-recovery traces and coverage gates remain.

For an identified negative control, invoke the unchanged helper from the
read-only source against a distinct disposable owner copy in container storage:

```bash
python3 "$OWNER_SOURCE/tests/endpoint-boundary/apply_lifecycle_fault.py" \
  "$FAULT" "$STAGED_OWNER"
INERTIA_NATIVE_MODE=lifecycle-counterexample \
INERTIA_LIFECYCLE_CASE="$CASE" INERTIA_NATIVE_ARTIFACTS="$FRESH_OUTPUT" \
  bash "$STAGED_OWNER/tests/endpoint-boundary/run.sh"
```

| Fault | Required selected cases | Required assertion |
| --- | --- | --- |
| `endpoint-retirement` | `endpoint-queued-move`, `endpoint-queued-scroll` | Lifecycle transition left an old stream active |
| `activity-retirement` | `activity-idle`, `activity-sleep` | Lifecycle transition left an old stream active |
| `first-stream-only` | `activity-sleep` | Lifecycle transition left an old stream active |
| `endpoint-admission` | `endpoint-observed-move`, `endpoint-observed-scroll` | Old endpoint inertia reached the replacement target |
| `noop-retirement` | `lifecycle-noop` | Initial or no-op lifecycle notification cancelled fresh motion |

Run each selected fault/case on both cores, with a fresh output directory per
entry. The owner command builds once and runs that exact binary three times,
requiring the named behavioral assertion in each nonzero, non-timeout result.
Compiler/dependency failures, unrelated crashes and missing markers do not count.
Focused selection cannot satisfy the complete canonical entry. Existing endpoint
error-recovery, shared-HID, manual-bypass, adapter, coverage/ABI and all13
invalid-DTS/backend controls remain required.

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
occur. The DYA fixture now selects pinned Custom Settings and enables the
adapter; stock does not. Supply `ZMK_TEST_CUSTOM_SETTINGS_ARCHIVE` for offline DYA.

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
DYA now owns the exact archive/digest admission path because its fixture enables
the adapter. Identity/archive bypass controls must break their rejection tests;
a tar parsing error is not accepted as the wrong-digest control.

## Retired simulation and physical gaps

The active `tests/bsim` tree was removed on 2026-10-05 after SHA256-verifying an
external archive of its source and raw ordered traces. No failed snapshot was
accepted. Both retired `split-glide` and `split-wlan` retain their arithmetic,
state and error assertions in the owner fixtures above. Their exact ordered
radio/HID/LED/battery traces, physical timing, congestion/reconnect, power and
retention remain gaps requiring matching production-board evidence. The
consuming keyboard retains archive locations and the replacement map; that
archive is not a prerequisite for current module checks.

## Responsibility map

No source-button policy/history or final-filter dependency exists here. Inertia
owns movement/scroll qualification, decay, fractional remainders, scheduled
output admission and cancellation. The source event handler preserves payload
and sync; a sync may close a buffered motion frame. Pending report generations
and one queued token prevent stale output or a burst after input-queue pressure.

Motion state, frame-open state, active/inertial flags, the mutex, work items and
pending generations have distinct roles. They do not duplicate final button
pair history. Same-node streams share manual cancellation; independent nodes
remain independent. Removing these fields would change the glide/cancellation
contract, so no production state/API/DTS/Kconfig is removed in this audit.

| Boundary | Retained fixture/assertions |
| --- | --- |
| Pure arithmetic/frame qualification | test_inertia_core.c links actual inertia_core.c: fixed-point, thresholds, frame/decay boundaries |
| Stream/motion helpers | runtime/helpers_harness.c: same reset, independent state, qualification, manual cancellation and arithmetic |
| Scheduled driver / queue admission | runtime/driver_harness.c: same ordered timing, first-axis cancellation, bounded pending frames, token generations, queue refusal/recovery, unlocked endpoint transaction |
| Guarded probe | runtime/test_hook_harness.c: same test-hook behavior |
| Real work/input/endpoint | endpoint-boundary: twenty-nine groups, three same-binary runs/core when changed; controlled endpoint is not physical transport |
| Actual other listener / shared HID | review-shared-hid: pending-report integrity, manual X before stale token/Y sync, exact first frame, final clearing; named controls retained |

Scheduled inertia output uses an input-thread token callback and its own HID
set/send/clear transaction. It can bypass the source listener's final filter.
That filter is not a global HID interceptor or glide scheduler; cancellation
and serialization must stay in this owner. Threaded input remains required;
unrelated HID writers outside it are not covered.

## Runtime settings checks

The existing lifted fixtures now derive actual production config/state types.
All previous assertions remain. API fixtures cover every scalar boundary and
coupled threshold, all 100 percentage/Q8 round trips, no-op writes, rejection,
independent nodes and queued-token invalidation. Whole-source adapter checks
cover twenty keys across two nodes, defaults/ranges/events, read/type errors,
rejected apply and duplicate keys, with executable coverage admission. Named
controls detect ignored apply and key/storage overflow.

Four added endpoint cases retain the original eight groups: move/scroll settings
updates discard queued tokens, and complete while an accepted endpoint report
is blocked. Committed reports finish intact and no later output escapes; setters
finish within one simulated ms in these controlled cases. This is not an input
queue or physical latency bound. Both cores run the twenty-nine-group binary three
times, alongside unchanged actual-listener/shared-HID and all 13 DTS/backend guards.
DYA runtime also tests real registry rejection/apply and NVS persistence across
three fresh first-boot/reload pairs of one binary. Actual Studio RPC/browser,
physical flash power-loss/endurance and physical delivery remain NOT RUN.

The readable `amgskobo__inertia` namespace and `move_period` / `scroll_period`
keys are asserted literally by the whole adapter fixture. `scroll_inertia` plus
`cancel_on_ctrl` exercises the exact 64-byte storage limit; overlong-key/storage
controls remain. DYA native persists and reloads both renamed period keys as
well as trigger wait and rejects lookup of the old namespace/interval keys.

## Selected workspace admission

The owner-local `integration/workspace-env.sh` derives `ZEPHYR_BASE`,
`Zephyr_DIR` and a nonempty `ZEPHYR_MODULES` from the selected workspace.
Discovery failure and empty module lists stop before compilation; ambient cache
modules cannot supply missing dependencies. The canonical host entry runs
`integration/test_workspace_env.py`, including failed-discovery/empty-list
regressions and a symlinked cache with paths containing spaces. The native entry
uses the same selection helper. This validates dependency selection, not hardware.

## Native-width timeout conversion

Millisecond trigger, report and queue-retry reservations use the same exact ceiling as Zephyr's
`K_MSEC`. Their inputs remain uint16 milliseconds. This conversion preserves
configured delays and the kernel tick ABI; the report/slot lifecycle is
described below. Clock rates through65536Hz use a checked uint32 numerator. Higher
rates split seconds and the fractional product before multiplication, retaining
the configured `k_ticks_t` width without software64-bit division. No new clock
restriction is introduced.

`runtime/timeout_harness.c` gates the two actual production functions. Every
wait0..65535 is compared with an independent wide ceiling reference at14 clock
rates, including32768Hz, the65536/65537 branch boundary andUINT32_MAX. The same
fixture executes on a32-bit compiler ABI with both32-bit and64-bit tick formats.
The object probe must contain no wide arithmetic helper. Identified floor,
unchecked-product and lost-fraction faults each must compile and fail qualified
C assertions three times per tick format. The compiled wide-divider control
retains numeric correctness but must fail the object-width guard. All previous
fixtures and coverage admission controls remain selected.

The additional thirteenth endpoint group calls a minimal test-config-only probe
of the production conversion. All65536 waits at7 positive clock rates are
compared with the actual pinned Zephyr conversion macros. The existing twelve
work/input/endpoint/settings groups and their timing/order assertions remain.
The guarded probe is absent from normal production firmware. Physical input,
radio/HID latency and CPU-cycle improvements remain unmeasured.

```bash
python3 tests/runtime/run.py --timeout /absolute/fresh/timeout.c
python3 -m unittest discover -s tests -p test_timeout_width.py -v
```

## Rearming an already-running decay handler

Four endpoint groups pause an actual work handler before it takes the state
mutex, then complete a new manual frame and resume the old handler. Move and
scroll each run from an armed frame and an active glide. No decay or report
may occur during the new 20 ms trigger wait; the first report must arrive
20–22 simulated ms after rearming with the exact expected axes and magnitude.
Cancellation then prohibits all further reports. The host fixture also checks
the tick immediately before expiry and the exact deadline with explicit time.

The pre-fix native binary fails the named
`rearmed inertia worker skipped trigger delay` assertion on three identical
runs. Its raw output and binary identity are review evidence, not a product
PASS. The test-only `inertia_test_before_decay` hook supplies the interleaving;
kernel cancellation, rescheduling and input-thread output use production APIs.
The other thirteen endpoint groups and all shared-HID/configuration cases
remain selected. Physical sensor and transport timing remain unmeasured.

Two further endpoint groups hold a work callback after it unlocks state but
before Zephyr clears its running flag. A new manual frame restarts the 20 ms
trigger, which expires during a controlled 35 ms hold. The new move/scroll work
must remain queued, emit the exact first report within 0–2 simulated ms of
release, and stop completely after cancellation. The pre-repair binary fails
`rearmed inertia work lost during cancellation` on three identical runs.

Cancellation now replaces a pending timer with `K_FOREVER` using the existing
reschedule API. It does not set Zephyr's `CANCELING` flag, which would reject
new submissions while an older callback is still running. Already queued
callbacks still check state and the latest timer before applying decay; the
generation check still rejects old input tokens. No timeout is added for
`K_FOREVER`. The helper assertions retain the same per-work disarm targets
previously checked by `was_cancelled`; state reset, generation, exact report
values/times and post-cancel silence assertions remain selected. This is the
intentional replacement of the kernel cancellation mechanism, not removal of
the output cancellation contract.

## Composite zero axes, report cadence and node isolation

The host driver fixture enumerates all 24 orders of a four-axis composite
report for pointer/scroll, both signs, Ctrl held/released and three initial
histories (fresh, preceding manual frame, active glide): 576 cases. Exact
axes, velocity averages, sync and payload are preserved. Zero-only frames and
nonzero mode changes still cancel, and a zero from another stream cannot reuse
the first stream's history. The real-listener/shared-HID fixture retains its
original three reports and appends two manual composite frames, their exact
first `(135, 54)` inertia frame, and a zero-only stop: seven ordered reports.

Four added endpoint groups hold the input consumer before token admission or
hold the actual HID setter after admission, in both move and scroll. The next
report must remain 100–102 simulated ms after the first in these controlled
fixtures, with exact decay values and no post-cancel report. The pre-repair
controls reproduce 9 ms and same-instant double output three times each.
Four more groups rearm manual input while the HID setter is held, preserving
an unexpired trigger or resuming one already overdue. These eight groups are
added to all nineteen retained groups. The host tests also exercise rejected
queued tokens and an owned in-flight slot across future/expired rearming.

`move_report.queued`, `scroll_report.queued` and the test snapshot's
`report_pending` mean slot ownership, including the accepted HID transaction.
The worker cannot reuse that slot or advance its decay. Completion restarts
the report interval for the same generation; a changed generation retains
its trigger or resumes immediately when overdue. Queue refusal still drops
one interval and retries, without creating a private backlog.

All twenty adapter keys route changes only to their owning node; unrelated
namespace/unknown-key events preserve both nodes. Initialization continues to
apply all nodes. The former fixture's second-node bool assertion is retained
under that node's own change event, rather than relying on another node's
notification. The DYA registry/NVS fixture changes a different node after a
direct runtime tune and checks that both its value and active generation
survive. The pre-repair native binary fails that named isolation assertion on
three fresh-flash runs; all existing persistence/reload assertions remain.

The adapter sanitizer run additionally enables GCC's pointer-comparison check.
The host registry now has one loop per foreach, matching the control-flow
contract of the real linked section. Duplicate checks use registry order, not
unrelated descriptor address ordering. This removes a host representation
dependency; its sanitizer failure is not counted as an observed firmware bug.

## Repeated output failures and complete glide traces

Eight host traces cover move/scroll, both signs and normal/faulted execution.
A fixed 50% decay has seven exact nonzero frames, a checked final stop tick,
and exact ordered axes and totals. The faulted traces drop two intervals at
input-queue admission, hold the following token past two periods, then fail
two endpoint submissions before recovering. Invalid/equal parameter writes
while the token is owned must preserve its generation and payload. Failed
and successful motion totals are checked separately; no output may follow
the stop tick.

Two more native endpoint groups hold the first submission for 120 simulated
ms and return `-EIO` for the first two attempts. With 50% decay, the exact
outputs are 32, 16 and 8. The next attempt must wait 100–102 simulated ms
after completion, including a failed submission. Slots and actual ZMK HID
storage must clear on error, recovery must send exactly once, and cancellation
must prohibit any later attempt. These extend the retained 27 groups to 29.
The same tests on the earlier driver must reject compressed recovery with
the named `failed endpoint compressed recovery interval` assertion. This is
controlled endpoint failure, not a physical disconnect/reconnect test.

For that earlier-source control only, set
`INERTIA_NATIVE_MODE=endpoint-error-counterexample` on the same endpoint entry.
It requires the named assertion, nonzero/non-timeout status and three runs of
the same binary; default mode requires the complete 29-group PASS marker.
