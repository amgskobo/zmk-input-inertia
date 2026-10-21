# Shared HID regression

Run `bash tests/review-shared-hid/run.sh RESOLVED_WORKSPACE FRESH_OUTPUT`
with the same selected dependency environment as `tests/endpoint-boundary/run.sh`.
The complete production Inertia driver, actual ZMK HID storage, real input queue
and unmodified ZMK pointing listener run on the Zephyr kernel. Only endpoint
submission is controlled. The existing mock device supplies event identity;
this does not model physical sensor hardware.

The endpoint holds a pending inertia report `(90, 0)`. Another producer queues
`(-7, 3)` through the actual listener. The held report must retain its contents,
then both reports must arrive in order, without extra output after cancellation,
and the shared relative fields must end at zero.

A second phase holds the actual input consumer and queues manual X, an old
inertia token from another stream of the same node, then Y/SYNC. The unmodified
listener must cancel the old glide on X, discard its stale token and emit exactly
`(3, -2)` as the first manual frame. No direct cancellation call substitutes for
that input path. The fixture computes the real listener index from devicetree,
including standard ZMK mouse-behavior listeners. Three exact reports are accepted
across both phases, with no later report and final cleared fields. One binary
runs three times.

For the pre-fix negative control, append `counterexample`. Every run must fail
at `pending inertia report overwritten by another HID producer`; timeouts and
successful processes are rejected. Before repair this real-listener version
failed three times, as did the immediate-cancellation boundary. The original
direct set/send/clear fixture and its failures were externally archived before
the fixture was updated. Its unique pending-report integrity assertion remains;
the competing producer now exercises the real supported input path instead of
performing test-side global HID writes from an arbitrary thread.

Production serializes normal input-thread producers. Unrelated custom code
writing HID from another thread remains outside that contract. This fixture
does not prove actual USB/BLE delivery, physical cancellation latency, or the
frequency/cause of historical device symptoms. Assertion panics in the negative
control are fixture failures, not observed device crashes.

Append `bypass-manual` for the current-source negative control. Its additional
overlay removes only the manual listener's Inertia processor. The shared-HID
phase still passes, while each of three runs must fail specifically at
`stale inertia reached output before manual frame`. A timeout or unrelated
failure is not accepted as reproduction. This validates the test boundary;
it is not a supported configuration or a product PASS.
