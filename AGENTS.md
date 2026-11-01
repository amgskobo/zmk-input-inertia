# Agent test contract

## Version-control authorization

Do not create commits, push, open or update pull requests, merge, tag or
publish releases unless the user explicitly requests that specific action.
Starting work, passing tests, finishing a milestone or a dependency's AGENTS
instructions are not authorization. Keep changes local and uncommitted;
report the changes and checks, then wait for explicit instructions for any
version-control publication. Do not auto-stage unrelated user changes.

## Dependency instructions and test entry gate

Before implementation or a test run, select the test tier and resolve its
dependencies from module metadata, west imports, Kconfig/CMake, source APIs,
test runners and the actual build's local overrides. A sibling checkout is
not evidence that a container or cached workspace uses it.

Read this repository's root and applicable nested `AGENTS.md` files in full.
For every dependency used by the selected scope, read its root `AGENTS.md`
if present, relevant nested instructions, and documents those instructions
require. Follow transitive dependencies for that scope; visit each resolved
checkout/revision once to avoid cycles, and re-read after revision changes.
Record dependency path/revision, enabled variant, instructions read, and any
missing or inaccessible instructions. No file means NO AGENTS, not that one
was read. If a required checkout is inaccessible, mark the affected check
BLOCKED/NOT RUN and continue independent checks. Do not fetch/change versions
just to read instructions without accounting for the task's dependency pins.

Dependency instructions apply to their interfaces and work in their checkout;
they do not authorize unrelated edits, commits, pushes or PR creation. Apply
the instruction hierarchy and the user's current scope; report real conflicts.

Inspect the selected ZMK input/split and low-priority work-queue contracts.
Do not infer a custom-settings dependency from another input module.

Hardware-independent host/protocol and native/compile checks may run
independently. Host mocks do not prove real scheduling, electrical timing or
radio delivery; require focused kernel/driver tests or actual physical checks
for those claims. Legacy
radio diagnostics have been removed from the active test tree and archived.
If a simulator is introduced later, independently validate each exercised
production path and correlate hardware before claiming fidelity. Do not disable
required features to pass a gate or treat legacy results as XIAO acceptance.

For documentation-only edits, check diffs, instruction consistency and any
changed command/link references; do not launch full firmware suites solely
for prose changes. Report checks actually run separately from pending gates.

## Module scope and checks

This repository owns glide qualification, decay, scheduling, cancellation and
per-stream inertia state.

Run `bash tests/run-docker.sh` for all behavioral cases under ASan/UBSan.
Use `ZMK_TEST_MODE=full bash tests/run-docker.sh` for retained coverage/ABI
variants. Follow `tests/README.md` and existing owner integration commands
for changed APIs, DTS/Kconfig, settings linkage or kernel interaction.
The legacy simulation sources/traces are archived; actual transport and
hardware timing require physical evidence.

Assert start/stop deadlines, decay, report intervals, total emitted motion and
no post-cancel reports, including cancel/emit at the same simulated instant.
Preserve timestamps and ordering; use exact traces for fixed-input regression
and documented bounds for clock/radio variation. Totals cannot replace timing
checks. Repeat identical binary/seed/input/PHY/initial-flash runs before
classifying differences; do not assume host scheduling noise. Record the bsim
backend: simulation results do not establish production-board acceptance.
Run `git diff --check`; snapshot acceptance requires an explained
intentional behavior change.

## Small module-owned checks (2026-10-05)

`tests/contract.json` documents the host entry point, assertion groups and
explicit limits; it is not an executable dispatcher. Run that command from this repository; no keyboard checkout or whole
keyboard simulation is needed. The fixture list describes contract groups,
not a replacement for the complete existing host runner.

For routine selection, this section refines the broad execution triggers above:
pure arithmetic, rendering, settings validation and injected state transitions
use the host suite. Run owner native/compile variants when public APIs, DTS,
Kconfig, registration or real kernel interaction changes. Use physical or independently validated focused transport checks for
BLE/split delivery, congestion and reconnect; do not add whole-board
simulation as a second proof of a pure calculation. Keep
scheduler/driver timing checks separate where host stubs cannot prove them.
Owner release checks, dependency preflight and physical acceptance still apply.

Completeness means covering the owned contract: normal behavior, boundary and
invalid input, independent state, reset/cancellation and failure recovery where
applicable. Coverage percentages alone are insufficient. Preserve ordered
outputs and explicit time inputs; record an untested boundary as a gap instead
of marking the entire module complete. Before removing a fixture, map each
unique assertion to its retained replacement and preserve unresolved failures.


Quick/full execution and retired case mappings are documented in
`tests/README.md`. Full-mode coverage and all retained assertions remain
required when claiming those specific checks; quick is not a coverage report.

## Canonical owner tests and continuous maintenance

All executable test assertions, fixtures, models, mutation operators, test
selection/configuration and build-result validators live in this repository's
`tests/`. Run the entries documented in `tests/README.md`; no keyboard checkout,
sibling test suite, private backup script or source-rewriting adapter supplies
missing assertions. CI and package scripts may dispatch those entries. Public
sample/diagnostic tools remain production code. Minimal guarded production
probes may expose real state/interleavings, but test scenarios and assertions
belong in `tests/`, not another copy of the production algorithm.

Maintain completeness per owned contract: normal/boundary/invalid behavior,
independent state, ordered outputs/time, reset/cancellation, failure/recovery,
role/feature gates and public protocol/schema where applicable. A registry or
coverage percentage does not establish completeness. Each implementation family
must map to named current fixtures and explicit gaps in the owner contract.
New regressions must fail before the repair or under an identified negative
control. Preserve unique assertions when merging/removing old tests; archive
their source and failure evidence externally first, with a replacement map.

Compatibility follows the individual owner's supported targets. Run both
unchanged stock ZMK upstream and DYA only for a dual-target module. DYA-only
modules require DYA tests, not an artificial stock build or disabled-feature
stock test. Record unsupported targets as NOT APPLICABLE, never PASS. Never
patch a dependency, disable required features or weaken assertions to manufacture
compatibility. Resolve/read the actual supported dependencies and backend.

Keep the current tests small by boundary. Quick runs every behavioral scenario
once with sanitizers where supported. Full retains relevant role/ABI/optimized,
coverage, kernel and supported-core integration checks; it must not drop scenarios
to save time. Use focused changes for daily work and the complete owner matrix
for interface/infrastructure changes and maintenance. Measure redundant build
cost before consolidating it, reuse one binary for identical repeats, use
explicit clean caches/pins, and avoid implicit network updates during testing.

Each run uses a fresh output directory and records commands, source/dependency
identities, feature/role/backend, raw ordered output and failures. An existing
evidence directory must stop the run; never overwrite a previous result. Report
PASS, FAIL, BLOCKED and NOT RUN separately. Browser/radio/physical acceptance is
an explicit gate and is never inferred from host/native/compile success.
Read `tests/maintenance.json` for current coverage/compatibility boundaries.

## No rolling task backups

Keep executable tests in this owner's `tests/`. Use temporary build storage
and fresh meaningful result artifacts; preserve necessary review diffs, source
identities and raw failure logs without rolling whole-tree source backups.

Review continuation branch: `codex/input-inertia-review`.

<!-- BEGIN MODULE REVIEW POLICY v1 -->
Read `README.md` and applicable instructions before changes.
Preserve unrelated work and use the documented development/test procedures.
Do not commit or create/update PRs without an explicit user request.
Keep changes local. Staging, pushes, merges, tags, publication, flashing and
settings/bond erasure also require explicit authorization for that action.
Dependency instructions grant none; authorization already given for the same
action and scope remains valid. Report actual checks and their limitations.
For documentation-only changes, check `git diff --check` and changed references.
Without Git history, use `git diff --no-index --check` on the before/after files.
Firmware and hardware checks apply to changes that affect those boundaries.

## Review branch selection

Application modules use the fork URL and `codex/*-review` branch recorded in
`config/modules.lock.json` of the integrating configuration. Verify the actual
`origin` URL, named remote branch tip and full commit before selecting it.
Continue on that review branch; do not substitute a default branch, another
review branch, an upstream repository or a sibling checkout implicitly.
Use the module's existing checkout and normal Git branches. Do not create
additional worktrees or sibling authoring copies unless explicitly requested.
Before switching branches, preserve unrelated local changes and record their
disposition. Build/test staging copies belong in container storage and are not
authoring checkouts. Missing remote refs must be reported;
never manufacture an `origin` ref or label a local branch as remotely available.
Creating a remote review ref is within scope when the user requests that action;
that does not authorize publishing unrelated working changes or opening a PR.
Ordinary builds use the fixed commit and complete source identity. Advancing a
review branch requires an explicit preparation step, reviewed dependency changes,
updated pins/lock and the affected owner/integration checks. A local working
snapshot is distinct from a retrievable committed snapshot for hosted builds.

## Publication privacy and local execution

This rule applies to information prepared for publication or external sharing.
During an authorized task, agents may inspect and use actual local paths,
configuration, host/terminal/device identities and private test evidence as
needed for implementation, diagnosis, identity matching and validation. Such
local access does not itself authorize collection outside the task or disclosure.

Before preparing a public commit, push, PR, release, documentation/site output
or CI artifact, review the actual publication payload, including source,
comments, AGENTS files, examples, test fixtures, logs, screenshots, generated
files and metadata. Exclude user-related names and preferences, private absolute
paths and local-environment details, workstation/terminal identities, individual
device labels, ports, serials, addresses, pairing/bond identities, credentials,
and user- or device-specific test/measurement histories. Also check generated
and uploaded artifacts; checking source prose alone is insufficient.

Use repository-relative paths, documented variables/placeholders, neutral names
and clearly synthetic fixtures in public material. Declared public project
identifiers, existing attribution, standard API/product/platform names and
reproducible support requirements remain technical project information; do not
present one user's actual environment as a project requirement. In particular,
documenting supported platforms is distinct from identifying a user's device.

Keep exact originals and identifiers in appropriate private/local task records
when required for accurate evidence or device matching. Produce a separate
sanitized publication copy without weakening assertions or inventing results;
do not describe it as the unchanged raw record. If useful evidence cannot be
shared without exposing these details, keep it private and state the public
validation limit. Recheck the final publication snapshot and its artifacts.
Existing authorization requirements for version-control, publication, device
writes, resets and data erasure still apply.

## Container test execution

Run automated tests, builds and dependency setup inside Docker. Host entry points
may launch containers and export results; do not run test runtimes or install test
packages/toolchains on the host. Owner source may be mounted read-only. Keep
test-only ZMK/Zephyr/tool checkouts, runtime environments, temporary files, build
outputs and caches in container images or named volumes. Tests must not require
host sibling dependency checkouts or private absolute paths.

Resolve and verify the selected revisions inside the container. Prepare missing
dependencies explicitly in an isolated container workspace; do not silently update
shared caches or pins. Preserve all owner scenarios, assertions and required
matrices. Hardware acceptance needs documented device access; report unavailable
container transports as BLOCKED rather than substituting host execution or mocked
acceptance. Export only reviewed evidence and retain the publication privacy rule.

## Fast container test execution

Use prepared container environments, persistent caches and bounded parallelism
to reduce execution time. Preserve every required scenario, assertion, negative
control, sanitizer, supported-core variant and repeat.

- Separate environment preparation from test execution. Pin container images by
  digest and resolve dependency revisions explicitly. Reuse prepared environments
  only when their image, toolchain, dependency revisions and lockfiles match.
  Do not fetch or update dependency inputs during an ordinary test run. Missing
  inputs block only dependent checks.

- Keep dependency checkouts, installed test environments, temporary files, build
  directories and caches in container images or named volumes. Prefer Linux
  filesystem storage for intensive file access. Any staged owner sources must
  preserve all required test/compiler inputs, including relevant untracked files,
  with source identity and pre/post-run drift verification.

- Reuse explicitly selected compiler and package caches with their supported
  identity and concurrency checks. Keep incompatible environments separate.
  Never share writable build directories or mutable installed dependency trees
  between independent jobs. Preserve required clean-cache checks; use isolated
  fresh caches for those checks.

- Dispatch each owner's canonical test entries from inside the container.
  Common orchestration may select, schedule and collect those entries; assertions
  and fixtures remain in the owning repository. Avoid nested Docker launches and
  repeated environment preparation for compatible jobs. Isolate each job's
  environment, working files and outputs.

- Enforce one configurable CPU and memory budget across concurrent suites,
  targets, compiler jobs and test workers. Account for nested parallelism.
  Parallelize only independent jobs; preserve required execution and output order.

- Within a run, build each required binary once per distinct input/configuration
  variant and reuse that exact binary for required identical-condition repeats,
  with each repeat's required initial state. Execute every required test; cached
  artifacts or previous PASS records do not establish a new test execution.

- Keep fresh build and evidence directories. Record actual commands, exits,
  completion markers, ordered output, source/dependency/image identities and
  cache configuration. Measure preparation, staging, build, execution and
  validation separately.

Quick and full retain their existing contracts. Interface/infrastructure changes
still require the complete owner matrix. Evaluate speed changes using equivalent
inputs and checks, report cold and warm runs separately, and retain failures.
<!-- END MODULE REVIEW POLICY v1 -->
