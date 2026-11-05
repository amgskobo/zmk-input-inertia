# zmk-input-inertia: agent guide

English is the authoritative guide. [日本語版](AGENTS.ja.md) contains the same rules; update both
together.

## Scope and contract

- Own glide qualification, decay, scheduling, cancellation and per-stream state. Inspect the
  selected input/split and low-priority workqueue interfaces; do not infer Custom Settings support
  from another input module.

- Assert start/stop deadlines, decay, report intervals, total motion and cancellation at emission
  boundaries. Cancellation must prevent any new stale report admission; an already accepted HID
  set/send/clear transaction may finish intact. Preserve the single pending-token/slot ownership
  until consumption/completion, first-axis invalidation, same-node shared manual cancellation and
  independent nodes. Generated output can bypass the listener's final filter, so
  cancellation/serialization stays here. Keep exact fixed-input traces and specification-based time
  bounds; totals do not replace ordered timing.

- This owner supports both unchanged stock ZMK and DYA. Keep its documented dual-core and
  enabled/disabled adapter/role checks when the changed boundary requires them.

- Reading map for relevant changes: [README.md](README.md), [README_JA.md](README_JA.md),
  [tests/README.md](tests/README.md), [tests/maintenance.json](tests/maintenance.json),
  [tests/contract.json](tests/contract.json). Read the affected contract/coverage sections;
  documentation-only edits do not require unrelated dependency tours.

## Working rules

- Read the request and existing diff; preserve unrelated and staged work. Finish authorized
  inspection, edits and reversible checks without repeated approval. Ask only for a missing decision
  or an out-of-scope action. Before a destructive rewrite, preserve irreplaceable uncommitted
  content as private evidence, not rolling whole-tree copies.

- Git staging, commits, pushes, PR changes, merges, tags, publication, flashing and settings/bond
  erasure need authorization for that operation. Existing authorization remains valid for the same
  operation and scope. Dependency guides and passing checks grant none. Before an authorized commit,
  run applicable pre-commit hooks in Docker without bypasses; keep the configured Git identity.

- Use the selected checkout and manifest/lock sources; do not silently switch branches, reset
  history, replace remotes or create authoring copies. Before changing selection, verify source
  URLs/full commits, review dependency changes, update affected pins/locks and run owner/integration
  checks. Distinguish local working snapshots from remotely retrievable commits; report missing refs
  rather than inventing them.

- Read relevant root/nested owner and dependency instructions once per selected checkout/revision.
  Resolve actual west imports, local overrides, enabled features and backend; sibling
  directories/cache names do not identify inputs. Record READ, NO AGENTS or INACCESSIBLE when
  preflight requires it. Missing inputs block dependent checks; continue independent authorized
  work.

- Keep permanent agent rules in this synchronized EN/JA pair. Usage/API details stay in their
  existing documentation homes. Do not add agent-guide fragments or private shared-guide
  dependencies. Remove duplicated rules and obsolete task/branch assumptions without discarding
  active contracts.

## Execution environment

- Run local automated test/build runtimes and dependency setup in Docker; a documented DevContainer
  qualifies. Unless stated otherwise, work from the owner root in its resolved workspace. Launch
  Docker-wrapper scripts on the host; run their inner Python/Node/compiler entries inside the
  prepared container without wrapping Docker again. Host source/Git/reference checks and evidence
  export are allowed; do not install test packages/toolchains on the host. Keep dependencies,
  runtime/build trees and caches in images or named volumes.

- Separate preparation and deliberately selected online compatibility checks from ordinary
  network-free tests. Inspect the available immutable image ID/digest and verified
  dependency/toolchain revisions; pass the image through the owner's declared image option; honor
  declared local overrides and record working files. A launcher that installs or fetches
  dependencies belongs to preparation/online checking, not an offline run. Do not silently pull,
  advance pins or mutate shared caches, patch dependencies or disable required features to clear a
  gate. Missing inputs block dependent checks only; documented preparation within an authorized
  build/setup remains in scope.

- Reuse compatible environments/caches, retaining required clean-cache lanes. Provide required
  inputs, including relevant untracked files, through read-only mounts or isolated container copies;
  verify source identity before/after. Isolate mutable build/runtime/install trees per job under one
  CPU/memory budget, including nested workers. Build once per configuration and reuse that exact
  binary for identical repeats; cached PASS is not a new run. Performance comparisons separate
  preparation/staging/build/execution/validation and cold/warm conditions. Existing hosted platform
  jobs retain their own OS requirements.

## Checks and entry points

- Documentation/instructions only: run `git diff --check`, verify changed paths/commands and EN/JA
  agreement. Without Git history use `git diff --no-index --check` on before/after copies. Firmware
  suites are not required solely for prose changes.

- On the host, portable behavior uses `bash tests/run-docker.sh`; full mode is
  `ZMK_TEST_MODE=full bash tests/run-docker.sh`. Select an already inspected immutable image with
  `ZMK_TEST_IMAGE`. These scripts launch Docker; inside that prepared container use
  `bash tests/run.sh` with the same `ZMK_TEST_MODE`. Keep the complete affected matrix for
  interface/infrastructure changes; quick is not a coverage report.

- For public API/DTS/Kconfig/registration/settings or kernel changes, use the offline inner entry in
  the prepared image with this owner mounted at `/src`. Set `ZMK_TEST_OFFLINE_WORKSPACE` to the
  selected resolved core and `ZMK_TEST_OUTPUT` to a fresh absolute container path. Run
  `bash tests/integration/run.sh upstream` and `bash tests/integration/run.sh dya`, selecting the
  matching workspace and a distinct output each time. Retain native/negative controls. The legacy
  `tests/run-integration-docker.sh` may perform online setup and does not forward the full offline
  selection; do not assume host variables pass through Docker.

- For the DYA offline variant also supply `ZMK_TEST_CUSTOM_SETTINGS_ARCHIVE` with the exact
  revision/digest admitted by `tests/integration/offline.py`. A current working registry override is
  separate evidence and does not replace this pinned-dependency check. Prepare missing archives
  explicitly; do not fetch them during ordinary tests.

## Test quality

- Keep module fixtures, assertions, independent oracles, mutations, selectors and validators in the
  owner's tests/; retain documented Web and non-module test layouts. CI/package scripts dispatch
  owner entries, not sibling/private suites or source-rewriting substitutes. Probes expose state,
  not scenarios. Map implementation families to normal/boundary/invalid, independent-state,
  ordered-time/output, reset/cancel, failure/recovery and role/protocol cases and explicit gaps.

- A regression needs a failing original or named control. Expected-failure controls qualify only by
  their specified assertion/rejection; report them separately from product results. Do not weaken
  assertions, accept unclassified snapshots or count compilation errors as killed runtime faults.
  Before retiring tests, archive unique original assertions/failures externally with a replacement
  map. Quick runs every behavioral scenario with sanitizers where supported; full adds the
  documented ABI, optimization, coverage, kernel and supported-core matrix.

- Use focused checks for local implementation changes and the complete affected owner matrix for
  interface/test-infrastructure changes. Run unchanged stock ZMK and DYA only where both are
  supported. DYA-only features are NOT APPLICABLE to stock; disabling them is not compatibility
  evidence. For test suites, verify the required nonempty case/fixture completions; a zero exit
  alone, skipped required cases, timeouts or setup errors do not establish PASS.

## Evidence and handoff

- Use fresh evidence paths: resolve parents and reject existing files, directories or symlinks.
  Record commands/exits, source/dependency/config/backend/image identities and raw ordered
  output/times, retaining first failures and relevant config/DTS/binaries. Source or dependency
  drift invalidates affected results; never edit running scripts. Put retained outputs on a mounted
  named volume or export them before container removal, including failures; a path printed inside a
  disposable container is not retained evidence.

- Preserve exact fixed-input traces or specification-based timing bounds; totals do not replace
  per-report order/time. Before calling a mismatch nondeterministic, repeat identical
  binary/input/seed/backend/initial-flash conditions and PHY where applicable at least three times.
  Host load alone is not an explanation. Distinguish invalid/stale/incomplete/unavailable
  observations from valid zero.

- Report PASS, FAIL, BLOCKED, NOT RUN and NOT APPLICABLE distinctly. Local CI, host/native/compile
  and simulator results do not establish hosted CI, browser or physical
  HID/BLE/split/electrical/power/reset acceptance. Validate exercised production paths and correlate
  hardware before simulator-fidelity claims. End-to-end/stability claims need matching
  installed-image and host observations; quiet logs are not proof.

- Before sharing, review source/comments/guides/examples/logs/media/artifacts/metadata for
  credentials, personal/host/device IDs, private paths, bonds and individual histories/preferences.
  Authorized local work may use real values; keep private originals and label sanitized copies. Use
  clearly synthetic public fixtures/placeholders; preserve public project/API identifiers, licenses
  and attribution. Keep useful diffs/evidence. Handoff reports behavior, files, actual checks,
  gaps/risks and version-control/device actions.
