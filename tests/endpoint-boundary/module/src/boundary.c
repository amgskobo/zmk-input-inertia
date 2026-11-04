/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include <drivers/input_processor.h>
#include <zmk/endpoints.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/hid.h>
#include <zmk-input-inertia/inertia_test.h>
#include <zmk-input-inertia/runtime.h>

static const struct device *const inertia = DEVICE_DT_GET(DT_NODELABEL(inertia_runtime_test));
static const struct device *const independent = DEVICE_DT_GET(DT_NODELABEL(inertia_independent));
static const struct device *const gate_device = DEVICE_DT_GET(DT_NODELABEL(kscan));
#define LIFECYCLE_NODE(node) DEVICE_DT_GET(node),
static const struct device *const lifecycle_nodes[] = {
    DT_FOREACH_STATUS_OKAY(zmk_input_processor_inertia, LIFECYCLE_NODE)};
#undef LIFECYCLE_NODE
BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(zmk_input_listener) >= 2,
             "Lifecycle coverage requires a nonzero listener stream");
static bool lifecycle_running;
static bool lifecycle_node(const struct device *dev) {
    for (size_t i = 0; i < ARRAY_SIZE(lifecycle_nodes); i++) {
        if (dev == lifecycle_nodes[i]) {
            return true;
        }
    }
    return false;
}
/* Endpoint readiness/identity is the controlled transport input. All original
 * kernel, threaded-input and actual ZMK HID assertions remain unchanged. */
static atomic_t endpoint_target = ATOMIC_INIT(ZMK_TRANSPORT_USB);
static atomic_t lifecycle_tokens;
static K_SEM_DEFINE(lifecycle_report_ready, 0, 1);
static atomic_val_t last_endpoint_target;
struct zmk_endpoint_instance __wrap_zmk_endpoint_get_selected(void);
struct zmk_endpoint_instance __wrap_zmk_endpoint_get_selected(void) {
    atomic_val_t target = atomic_get(&endpoint_target);
    return (struct zmk_endpoint_instance){.transport = (enum zmk_transport)(target & 3),
                                          .ble.profile_index = (int)(target >> 2)};
}
static void lifecycle_catchall(struct input_event *event, void *context) {
    ARG_UNUSED(context);
    if (lifecycle_node(event->dev) && event->type == INPUT_EV_MSC) {
        atomic_inc(&lifecycle_tokens);
    }
}
INPUT_CALLBACK_DEFINE(NULL, lifecycle_catchall, NULL);
static K_SEM_DEFINE(input_entered, 0, 1);
static K_SEM_DEFINE(input_release, 0, 1);
static void block_input(struct input_event *event, void *user_data) {
    ARG_UNUSED(event);
    ARG_UNUSED(user_data);
    k_sem_give(&input_entered);
    __ASSERT_NO_MSG(k_sem_take(&input_release, K_SECONDS(2)) == 0);
}
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(kscan)), block_input, NULL);
static K_SEM_DEFINE(send_entered, 0, 1);
static K_SEM_DEFINE(send_release, 0, 1);
static K_SEM_DEFINE(cancel_start, 0, 1);
static K_SEM_DEFINE(cancel_done, 0, 1);
static K_SEM_DEFINE(decay_entered, 0, 1);
static K_SEM_DEFINE(decay_release, 0, 1);
static atomic_t hold_decay;
static K_SEM_DEFINE(decay_exiting, 0, 1);
static K_SEM_DEFINE(decay_finish, 0, 1);
static atomic_t hold_decay_exit;
static K_SEM_DEFINE(hid_set_entered, 0, 1);
static K_SEM_DEFINE(hid_set_release, 0, 1);
static atomic_t hold_hid_set;
static atomic_t hold_send, cancel_in_hook, cancelling, completed, reports;
static atomic_t endpoint_failures_left, endpoint_failures;
static uint16_t axis;
static int64_t cancel_begin, cancel_end;
static int64_t last_report_at;
static struct zmk_hid_mouse_report_body last_report;

static void process(uint16_t code, int32_t value) {
    struct input_event event = {.type = INPUT_EV_REL, .code = code, .value = value, .sync = true};
    struct zmk_input_processor_state state = {.input_device_index = 0};
    __ASSERT_NO_MSG(zmk_input_processor_handle_event(inertia, &event, 0, 0, &state) ==
                    ZMK_INPUT_PROC_CONTINUE);
}

void inertia_test_before_decay(const struct device *dev, size_t stream, bool scroll) {
    ARG_UNUSED(stream);
    ARG_UNUSED(scroll);
    if (dev == inertia && atomic_cas(&hold_decay, 1, 0)) {
        k_sem_give(&decay_entered);
        __ASSERT_NO_MSG(k_sem_take(&decay_release, K_SECONDS(1)) == 0);
    }
}

void inertia_test_after_decay(const struct device *dev, size_t stream, bool scroll) {
    ARG_UNUSED(stream);
    ARG_UNUSED(scroll);
    if (dev == inertia && atomic_cas(&hold_decay_exit, 1, 0)) {
        k_sem_give(&decay_exiting);
        __ASSERT_NO_MSG(k_sem_take(&decay_finish, K_SECONDS(1)) == 0);
    }
}

void inertia_test_before_emit(const struct device *dev, size_t stream, bool scroll) {
    if (dev != inertia && dev != independent) {
        __ASSERT_NO_MSG(lifecycle_running && lifecycle_node(dev) && stream < 2);
        return;
    }
    __ASSERT_NO_MSG(dev == inertia || dev == independent);
    __ASSERT_NO_MSG(stream < 2);
    if (dev == independent) {
        return;
    }
    if (atomic_cas(&cancel_in_hook, 1, 0)) {
        process(scroll ? INPUT_REL_WHEEL : INPUT_REL_X, 0);
    }
}

int __wrap_zmk_endpoint_send_mouse_report(void);
void __real_zmk_hid_mouse_movement_set(int16_t x, int16_t y);
void __real_zmk_hid_mouse_scroll_set(int16_t x, int16_t y);
void __wrap_zmk_hid_mouse_movement_set(int16_t x, int16_t y);
void __wrap_zmk_hid_mouse_scroll_set(int16_t x, int16_t y);
static void pause_hid_set(void) {
    if (atomic_cas(&hold_hid_set, 1, 0)) {
        k_sem_give(&hid_set_entered);
        __ASSERT_NO_MSG(k_sem_take(&hid_set_release, K_SECONDS(1)) == 0);
    }
}
void __wrap_zmk_hid_mouse_movement_set(int16_t x, int16_t y) {
    __real_zmk_hid_mouse_movement_set(x, y);
    if (x != 0 || y != 0) {
        pause_hid_set();
    }
}
void __wrap_zmk_hid_mouse_scroll_set(int16_t x, int16_t y) {
    __real_zmk_hid_mouse_scroll_set(x, y);
    if (x != 0 || y != 0) {
        pause_hid_set();
    }
}
int __wrap_zmk_endpoint_send_mouse_report(void) {
    /* Substitute only the transport boundary. The complete production driver,
     * actual ZMK HID storage and real kernel mutex/work/thread APIs are used. */
    __ASSERT_NO_MSG(!atomic_get(&completed));
    const struct zmk_hid_mouse_report_body *body = &zmk_hid_get_mouse_report()->body;
    __ASSERT_NO_MSG(axis == INPUT_REL_X ? body->d_x != 0 : body->d_scroll_y != 0);
    atomic_inc(&reports);
    last_report = *body;
    last_report_at = k_uptime_get();
    last_endpoint_target = atomic_get(&endpoint_target);
    k_sem_give(&lifecycle_report_ready);
    if (atomic_cas(&hold_send, 1, 0)) {
        k_sem_give(&send_entered);
        __ASSERT_NO_MSG(k_sem_take(&send_release, K_SECONDS(1)) == 0);
    }
    /* Acceptance precedes the controlled hold. A submitted report cannot be retracted. */
    printk("REPORT axis=%u x=%d y=%d wheel=%d hwheel=%d time=%lld\n", axis, body->d_x, body->d_y,
           body->d_scroll_y, body->d_scroll_x, k_uptime_get());
    if (atomic_get(&endpoint_failures_left) > 0) {
        atomic_dec(&endpoint_failures_left);
        atomic_inc(&endpoint_failures);
        return -EIO;
    }
    return 0;
}

static void cancel_thread(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    for (;;) {
        k_sem_take(&cancel_start, K_FOREVER);
        cancel_begin = k_uptime_get();
        atomic_set(&cancelling, 1);
        process(axis, 0);
        cancel_end = k_uptime_get();
        atomic_set(&completed, 1);
        k_sem_give(&cancel_done);
    }
}

K_THREAD_DEFINE(cancel_worker, 2048, cancel_thread, NULL, NULL, NULL, 0, 0, 0);

static struct inertia_test_snapshot snapshot(bool scroll) {
    struct inertia_test_snapshot s;
    __ASSERT_NO_MSG(inertia_test_get_snapshot(inertia, 0, scroll, &s) == 0);
    return s;
}

static void run_case(bool scroll, bool blocked) {
    axis = scroll ? INPUT_REL_WHEEL : INPUT_REL_X;
    atomic_set(&completed, 0);
    atomic_set(&cancelling, 0);
    atomic_set(&reports, 0);
    atomic_set(&hold_send, blocked);
    atomic_set(&cancel_in_hook, !blocked);
    process(axis, scroll ? 40 : 200);
    if (!blocked) {
        k_sleep(K_MSEC(40));
        __ASSERT_NO_MSG(!atomic_get(&cancel_in_hook));
        __ASSERT_NO_MSG(atomic_get(&reports) == 0);
        __ASSERT_NO_MSG(!snapshot(scroll).active);
        printk("PASS cancel-before-emit axis=%u time=%lld\n", axis, k_uptime_get());
        return;
    }
    __ASSERT_NO_MSG(k_sem_take(&send_entered, K_SECONDS(1)) == 0);
    k_sem_give(&cancel_start);
    /* The higher-priority cancel thread enters the real driver before this
     * thread resumes. Hold the transport for a controlled 120 simulated ms.
     * This duration is test input, not a claimed BLE deadline. */
    __ASSERT_NO_MSG(atomic_get(&cancelling));
    k_sleep(K_MSEC(120));
    bool waited = !atomic_get(&completed);
    k_sem_give(&send_release);
    __ASSERT_NO_MSG(k_sem_take(&cancel_done, K_SECONDS(1)) == 0);
    __ASSERT(!waited && cancel_end - cancel_begin <= 1,
             "blocked transport delayed new pointer cancellation");
    __ASSERT_NO_MSG(atomic_get(&reports) == 1);
    __ASSERT_NO_MSG(!snapshot(scroll).active);
    k_sleep(K_MSEC(130));
    __ASSERT_NO_MSG(atomic_get(&reports) == 1);
    const struct zmk_hid_mouse_report_body *body = &zmk_hid_get_mouse_report()->body;
    __ASSERT_NO_MSG(body->d_x == 0 && body->d_y == 0 && body->d_scroll_x == 0 &&
                    body->d_scroll_y == 0);
    printk("PASS blocked-send axis=%u cancel_waited=%d cancel_begin=%lld cancel_end=%lld "
           "wait_ms=%lld time=%lld\n",
           axis, waited, cancel_begin, cancel_end, cancel_end - cancel_begin, k_uptime_get());
}

static void gate_start(void) {
    __ASSERT_NO_MSG(input_report(gate_device, INPUT_EV_MSC, 0, 0, false, K_NO_WAIT) == 0);
    __ASSERT_NO_MSG(k_sem_take(&input_entered, K_SECONDS(1)) == 0);
}

static void run_queued_cancel(bool scroll) {
    axis = scroll ? INPUT_REL_WHEEL : INPUT_REL_X;
    atomic_set(&completed, 0);
    atomic_set(&reports, 0);
    uint32_t before = snapshot(scroll).emit_count;
    gate_start();
    process(axis, scroll ? 40 : 200);
    for (int i = 0; i < 60 && !snapshot(scroll).report_pending; i++) {
        k_sleep(K_MSEC(1));
    }
    __ASSERT_NO_MSG(snapshot(scroll).report_pending);
    int16_t saved = snapshot(scroll).velocity[scroll ? INERTIA_AXIS_Y : INERTIA_AXIS_X];
    k_sleep(K_MSEC(130)); /* a late consumer must not accumulate catch-up output */
    __ASSERT_NO_MSG(snapshot(scroll).report_pending);
    __ASSERT_NO_MSG(snapshot(scroll).velocity[scroll ? INERTIA_AXIS_Y : INERTIA_AXIS_X] == saved);
    struct zmk_input_processor_state other = {.input_device_index = 1};
    struct input_event manual = {
        .type = INPUT_EV_REL, .code = INPUT_REL_X, .value = 3, .sync = false};
    __ASSERT_NO_MSG(zmk_input_processor_handle_event(inertia, &manual, 0, 0, &other) ==
                    ZMK_INPUT_PROC_CONTINUE);
    __ASSERT_NO_MSG(manual.value == 3 && !manual.sync);
    __ASSERT_NO_MSG(!snapshot(scroll).active); /* stop on first axis, before sync */
    manual.code = INPUT_REL_Y;
    manual.value = -2;
    manual.sync = true;
    __ASSERT_NO_MSG(zmk_input_processor_handle_event(inertia, &manual, 0, 0, &other) ==
                    ZMK_INPUT_PROC_CONTINUE);
    __ASSERT_NO_MSG(manual.value == -2);
    k_sem_give(&input_release);
    k_sleep(K_MSEC(130));
    __ASSERT_NO_MSG(!snapshot(scroll).report_pending);
    __ASSERT_NO_MSG(snapshot(scroll).emit_count == before && atomic_get(&reports) == 0);
    printk("PASS queued-cancel axis=%u first_manual_axis_preserved=1 reports=0 time=%lld\n", axis,
           k_uptime_get());
}

static void run_independent_node(void) {
    axis = INPUT_REL_WHEEL;
    atomic_set(&completed, 0);
    atomic_set(&reports, 0);
    struct zmk_input_processor_state state = {.input_device_index = 0};
    struct input_event event = {
        .type = INPUT_EV_REL, .code = INPUT_REL_WHEEL, .value = 40, .sync = true};
    __ASSERT_NO_MSG(zmk_input_processor_handle_event(independent, &event, 0, 0, &state) ==
                    ZMK_INPUT_PROC_CONTINUE);
    process(INPUT_REL_X, 3);
    struct inertia_test_snapshot other;
    __ASSERT_NO_MSG(inertia_test_get_snapshot(independent, 0, true, &other) == 0);
    __ASSERT_NO_MSG(other.active && !other.inertial);
    k_sleep(K_MSEC(50));
    __ASSERT_NO_MSG(inertia_test_get_snapshot(independent, 0, true, &other) == 0);
    __ASSERT_NO_MSG(other.active && other.inertial && other.emit_count == 1);
    __ASSERT_NO_MSG(atomic_get(&reports) == 1);
    process(INPUT_REL_X, 3); /* manual pointer must also preserve active scroll decay */
    __ASSERT_NO_MSG(inertia_test_get_snapshot(independent, 0, true, &other) == 0);
    __ASSERT_NO_MSG(other.active && other.inertial);
    event.value = 0;
    __ASSERT_NO_MSG(zmk_input_processor_handle_event(independent, &event, 0, 0, &state) ==
                    ZMK_INPUT_PROC_CONTINUE);
    k_sleep(K_MSEC(130));
    __ASSERT_NO_MSG(atomic_get(&reports) == 1);
    printk("PASS independent scroll node survives manual pointer time=%lld\n", k_uptime_get());
}

static void run_queue_full(void) {
    axis = INPUT_REL_X;
    atomic_set(&completed, 0);
    atomic_set(&reports, 0);
    gate_start();
    for (int i = 0; i < CONFIG_INPUT_QUEUE_MAX_MSGS; i++) {
        /* These non-sync events fill the real queue without generating HID. */
        __ASSERT_NO_MSG(input_report(independent, INPUT_EV_KEY, 0, 0, false, K_NO_WAIT) == 0);
    }
    process(INPUT_REL_X, 200);
    k_sleep(K_MSEC(50));
    __ASSERT_NO_MSG(snapshot(false).active && !snapshot(false).report_pending);
    __ASSERT_NO_MSG(atomic_get(&reports) == 0);
    k_sem_give(&input_release);
    k_sleep(K_MSEC(90));
    __ASSERT_NO_MSG(atomic_get(&reports) == 1);
    process(INPUT_REL_X, 0);
    k_sleep(K_MSEC(130));
    __ASSERT_NO_MSG(atomic_get(&reports) == 1);
    printk("PASS full real input queue recovers without burst time=%lld\n", k_uptime_get());
}

static void run_settings_cancel(bool scroll, bool blocked) {
    axis = scroll ? INPUT_REL_WHEEL : INPUT_REL_X;
    atomic_set(&completed, 0);
    atomic_set(&reports, 0);
    atomic_set(&hold_send, blocked);
    struct inertia_params original, changed, other_before, other_after;
    __ASSERT_NO_MSG(inertia_get_params(inertia, &original) == 0);
    __ASSERT_NO_MSG(inertia_get_params(independent, &other_before) == 0);
    if (!blocked)
        gate_start();
    process(axis, scroll ? 40 : 200);
    if (blocked) {
        __ASSERT_NO_MSG(k_sem_take(&send_entered, K_SECONDS(1)) == 0);
    } else {
        for (int i = 0; i < 60 && !snapshot(scroll).report_pending; i++)
            k_sleep(K_MSEC(1));
        __ASSERT_NO_MSG(snapshot(scroll).report_pending);
    }
    changed = original;
    changed.trigger_ms++;
    int64_t begin = k_uptime_get();
    __ASSERT_NO_MSG(inertia_set_params(inertia, &changed) == 0);
    __ASSERT(k_uptime_get() - begin <= 1, "blocked endpoint delayed setting application");
    __ASSERT(!snapshot(scroll).active, "settings failed to cancel queued inertia");
    __ASSERT_NO_MSG(inertia_get_params(independent, &other_after) == 0 &&
                    other_after.trigger_ms == other_before.trigger_ms);
    atomic_set(&completed, 1);
    k_sem_give(blocked ? &send_release : &input_release);
    k_sleep(K_MSEC(150));
    __ASSERT_NO_MSG(atomic_get(&reports) == (blocked ? 1 : 0));
    __ASSERT_NO_MSG(!snapshot(scroll).report_pending);
    const struct zmk_hid_mouse_report_body *body = &zmk_hid_get_mouse_report()->body;
    __ASSERT_NO_MSG(body->d_x == 0 && body->d_y == 0 && body->d_scroll_x == 0 &&
                    body->d_scroll_y == 0);
    __ASSERT_NO_MSG(inertia_set_params(inertia, &original) == 0);
    printk("PASS settings cancel axis=%u blocked=%d reports=%ld time=%lld\n", axis, blocked,
           (long)atomic_get(&reports), k_uptime_get());
}

static void run_timeout_conversion(void) {
    static const uint32_t rates[] = {1, 100, 1000, 32768, 65536, 65537, UINT32_MAX};
    for (unsigned h = 0; h < ARRAY_SIZE(rates); ++h) {
        const uint32_t hz = rates[h];
        for (uint32_t ms = 0; ms <= UINT16_MAX; ++ms) {
            const k_ticks_t expected =
                (k_ticks_t)(IS_ENABLED(CONFIG_TIMEOUT_64BIT)
                                ? z_tmcvt_64(ms, 1000U, hz, true, true, false)
                                : z_tmcvt_32(ms, 1000U, hz, true, true, false));
            __ASSERT_NO_MSG(inertia_test_timeout_ticks((uint16_t)ms, hz) == expected);
        }
    }
    printk("PASS timeout conversion all65536 waits at7 rates matches real Zephyr\n");
}

static void run_running_rearm(bool scroll, bool inertial) {
    axis = scroll ? INPUT_REL_WHEEL : INPUT_REL_X;
    atomic_set(&completed, 0);
    atomic_set(&reports, 0);
    process(axis, scroll ? 40 : 200);
    if (inertial) {
        k_sleep(K_MSEC(50));
        __ASSERT_NO_MSG(snapshot(scroll).inertial && atomic_get(&reports) == 1);
    }
    atomic_set(&hold_decay, 1);
    __ASSERT_NO_MSG(k_sem_take(&decay_entered, K_SECONDS(1)) == 0);
    int before = atomic_get(&reports);
    /* An already-running work callback cannot be stopped by asynchronous
     * cancellation. New input must still get its complete trigger delay. */
    process(axis, scroll ? 16 : 80);
    int64_t restarted = k_uptime_get();
    struct inertia_test_snapshot armed = snapshot(scroll);
    __ASSERT_NO_MSG(armed.active && !armed.inertial && !armed.report_pending);
    k_sem_give(&decay_release);
    k_sleep(K_MSEC(10)); /* trigger-ms is 20; the old handler resumes at once. */
    __ASSERT(atomic_get(&reports) == before && !snapshot(scroll).inertial,
             "rearmed inertia worker skipped trigger delay");
    __ASSERT_NO_MSG(snapshot(scroll).ema[scroll ? INERTIA_AXIS_Y : INERTIA_AXIS_X] ==
                    armed.ema[scroll ? INERTIA_AXIS_Y : INERTIA_AXIS_X]);
    k_sleep(K_MSEC(25));
    __ASSERT_NO_MSG(atomic_get(&reports) == before + 1 && snapshot(scroll).inertial);
    __ASSERT_NO_MSG(last_report_at >= restarted + 20 && last_report_at <= restarted + 22);
    __ASSERT_NO_MSG(last_report.d_x == (scroll ? 0 : inertial ? 36 : 81));
    __ASSERT_NO_MSG(last_report.d_scroll_y == (scroll ? inertial ? 7 : 15 : 0));
    __ASSERT_NO_MSG(last_report.d_y == 0 && last_report.d_scroll_x == 0);
    const int64_t first_report = last_report_at;
    process(axis, 0);
    k_sleep(K_MSEC(130));
    __ASSERT_NO_MSG(atomic_get(&reports) == before + 1);
    printk("PASS running-rearm axis=%u inertial=%d restarted=%lld first_report=%lld time=%lld\n",
           axis, inertial, restarted, first_report, k_uptime_get());
}

static void run_rearm_expired_before_exit(bool scroll) {
    axis = scroll ? INPUT_REL_WHEEL : INPUT_REL_X;
    atomic_set(&completed, 0);
    atomic_set(&reports, 0);
    atomic_set(&hold_decay_exit, 1);
    process(axis, scroll ? 40 : 200);
    __ASSERT_NO_MSG(k_sem_take(&decay_exiting, K_SECONDS(1)) == 0);
    __ASSERT_NO_MSG(snapshot(scroll).inertial);
    for (int i = 0; i < 10 && atomic_get(&reports) == 0; i++) {
        k_sleep(K_MSEC(1));
    }
    __ASSERT_NO_MSG(atomic_get(&reports) == 1);
    /* The callback has unlocked state, but Zephyr still marks its work as
     * running. Keep it preempted past the newly armed trigger deadline. */
    process(axis, scroll ? 16 : 80);
    int64_t restarted = k_uptime_get();
    k_sleep(K_MSEC(35));
    __ASSERT_NO_MSG(snapshot(scroll).active && !snapshot(scroll).inertial);
    __ASSERT_NO_MSG(atomic_get(&reports) == 1);
    int64_t released = k_uptime_get();
    k_sem_give(&decay_finish);
    k_sleep(K_MSEC(10));
    __ASSERT(atomic_get(&reports) == 2 && snapshot(scroll).inertial,
             "rearmed inertia work lost during cancellation");
    __ASSERT_NO_MSG(last_report_at >= released && last_report_at <= released + 2);
    __ASSERT_NO_MSG(last_report.d_x == (scroll ? 0 : 36));
    __ASSERT_NO_MSG(last_report.d_scroll_y == (scroll ? 7 : 0));
    __ASSERT_NO_MSG(last_report.d_y == 0 && last_report.d_scroll_x == 0);
    process(axis, 0);
    k_sleep(K_MSEC(130));
    __ASSERT_NO_MSG(atomic_get(&reports) == 2);
    printk("PASS expired-rearm axis=%u restarted=%lld released=%lld first_report=%lld time=%lld\n",
           axis, restarted, released, last_report_at, k_uptime_get());
}

static void run_delayed_consumer_period(bool scroll) {
    axis = scroll ? INPUT_REL_WHEEL : INPUT_REL_X;
    atomic_set(&completed, 0);
    atomic_set(&reports, 0);
    gate_start();
    process(axis, scroll ? 40 : 200);
    for (int i = 0; i < 60 && !snapshot(scroll).report_pending; i++) {
        k_sleep(K_MSEC(1));
    }
    __ASSERT_NO_MSG(snapshot(scroll).report_pending);
    k_sleep(K_MSEC(90)); /* Resume just before the next 100 ms work deadline. */
    __ASSERT_NO_MSG(atomic_get(&reports) == 0);
    k_sem_give(&input_release);
    k_sleep(K_MSEC(50));
    __ASSERT(atomic_get(&reports) == 1, "delayed consumer compressed inertia interval");
    int64_t first = last_report_at;
    __ASSERT_NO_MSG(last_report.d_x == (scroll ? 0 : 90));
    __ASSERT_NO_MSG(last_report.d_scroll_y == (scroll ? 17 : 0));
    k_sleep(K_MSEC(65));
    __ASSERT_NO_MSG(atomic_get(&reports) == 2);
    __ASSERT_NO_MSG(last_report_at - first >= 100 && last_report_at - first <= 102);
    __ASSERT_NO_MSG(last_report.d_x == (scroll ? 0 : 81));
    __ASSERT_NO_MSG(last_report.d_scroll_y == (scroll ? 14 : 0));
    int64_t second = last_report_at;
    process(axis, 0);
    k_sleep(K_MSEC(130));
    __ASSERT_NO_MSG(atomic_get(&reports) == 2);
    printk("PASS delayed-consumer-period axis=%u first=%lld second=%lld time=%lld\n", axis, first,
           second, k_uptime_get());
}

static void run_hid_handoff_period(bool scroll) {
    axis = scroll ? INPUT_REL_WHEEL : INPUT_REL_X;
    atomic_set(&completed, 0);
    atomic_set(&reports, 0);
    atomic_set(&hold_hid_set, 1);
    process(axis, scroll ? 40 : 200);
    __ASSERT_NO_MSG(k_sem_take(&hid_set_entered, K_SECONDS(1)) == 0);
    k_sleep(K_MSEC(120)); /* Preempt the input thread after admission, before send. */
    __ASSERT_NO_MSG(atomic_get(&reports) == 0);
    k_sem_give(&hid_set_release);
    k_sleep(K_MSEC(50));
    __ASSERT(atomic_get(&reports) == 1, "HID handoff compressed inertia interval");
    int64_t first = last_report_at;
    __ASSERT_NO_MSG(last_report.d_x == (scroll ? 0 : 90));
    __ASSERT_NO_MSG(last_report.d_scroll_y == (scroll ? 17 : 0));
    k_sleep(K_MSEC(65));
    __ASSERT_NO_MSG(atomic_get(&reports) == 2);
    __ASSERT_NO_MSG(last_report_at - first >= 100 && last_report_at - first <= 102);
    __ASSERT_NO_MSG(last_report.d_x == (scroll ? 0 : 81));
    __ASSERT_NO_MSG(last_report.d_scroll_y == (scroll ? 14 : 0));
    int64_t second = last_report_at;
    process(axis, 0);
    k_sleep(K_MSEC(130));
    __ASSERT_NO_MSG(atomic_get(&reports) == 2);
    printk("PASS hid-handoff-period axis=%u first=%lld second=%lld time=%lld\n", axis, first,
           second, k_uptime_get());
}

static void run_hid_handoff_rearm(bool scroll, bool expired) {
    axis = scroll ? INPUT_REL_WHEEL : INPUT_REL_X;
    atomic_set(&completed, 0);
    atomic_set(&reports, 0);
    atomic_set(&hold_hid_set, 1);
    process(axis, scroll ? 40 : 200);
    __ASSERT_NO_MSG(k_sem_take(&hid_set_entered, K_SECONDS(1)) == 0);
    process(axis, scroll ? 16 : 80);
    int64_t restarted = k_uptime_get();
    k_sleep(K_MSEC(expired ? 35 : 5));
    int64_t released = k_uptime_get();
    k_sem_give(&hid_set_release);
    k_sleep(K_MSEC(5));
    if (!expired) {
        __ASSERT(atomic_get(&reports) == 1, "HID completion overwrote a future manual trigger");
        k_sleep(K_MSEC(20));
    }
    __ASSERT(atomic_get(&reports) == 2, "HID completion lost a rearmed manual trigger");
    __ASSERT_NO_MSG(last_report_at >= (expired ? released : restarted + 20));
    __ASSERT_NO_MSG(last_report_at <= (expired ? released + 2 : restarted + 22));
    __ASSERT_NO_MSG(last_report.d_x == (scroll ? 0 : 36));
    __ASSERT_NO_MSG(last_report.d_scroll_y == (scroll ? 7 : 0));
    int64_t first_new = last_report_at;
    process(axis, 0);
    k_sleep(K_MSEC(130));
    __ASSERT_NO_MSG(atomic_get(&reports) == 2);
    printk("PASS hid-handoff-rearm axis=%u expired=%d restarted=%lld released=%lld new=%lld "
           "time=%lld\n",
           axis, expired, restarted, released, first_new, k_uptime_get());
}

static void run_endpoint_error_recovery(bool scroll) {
    axis = scroll ? INPUT_REL_WHEEL : INPUT_REL_X;
    atomic_set(&completed, 0);
    atomic_set(&reports, 0);
    atomic_set(&hold_send, 1);
    atomic_set(&endpoint_failures_left, 2);
    atomic_set(&endpoint_failures, 0);
    struct inertia_params original, changed;
    __ASSERT_NO_MSG(inertia_get_params(inertia, &original) == 0);
    changed = original;
    changed.move_decay_factor_int = changed.scroll_decay_factor_int = 50;
    __ASSERT_NO_MSG(inertia_set_params(inertia, &changed) == 0);
    process(axis, 128);
    __ASSERT_NO_MSG(k_sem_take(&send_entered, K_SECONDS(1)) == 0);
    __ASSERT_NO_MSG(atomic_get(&reports) == 1);
    __ASSERT_NO_MSG(last_report.d_x == (scroll ? 0 : 32));
    __ASSERT_NO_MSG(last_report.d_scroll_y == (scroll ? 32 : 0));
    k_sleep(K_MSEC(120)); /* Error arrives after a complete report period. */
    int64_t released = k_uptime_get();
    k_sem_give(&send_release);
    k_sleep(K_MSEC(50));
    __ASSERT(atomic_get(&reports) == 1 && atomic_get(&endpoint_failures) == 1,
             "failed endpoint compressed recovery interval");
    __ASSERT_NO_MSG(!snapshot(scroll).report_pending);
    const struct zmk_hid_mouse_report_body *body = &zmk_hid_get_mouse_report()->body;
    __ASSERT_NO_MSG(body->d_x == 0 && body->d_y == 0 && body->d_scroll_x == 0 &&
                    body->d_scroll_y == 0);
    k_sleep(K_MSEC(65));
    __ASSERT_NO_MSG(atomic_get(&reports) == 2 && atomic_get(&endpoint_failures) == 2);
    __ASSERT_NO_MSG(last_report_at >= released + 100 && last_report_at <= released + 102);
    __ASSERT_NO_MSG(last_report.d_x == (scroll ? 0 : 16));
    __ASSERT_NO_MSG(last_report.d_scroll_y == (scroll ? 16 : 0));
    int64_t second = last_report_at;
    k_sleep(K_MSEC(100));
    __ASSERT_NO_MSG(atomic_get(&reports) == 3 && atomic_get(&endpoint_failures) == 2);
    __ASSERT_NO_MSG(last_report_at >= second + 100 && last_report_at <= second + 102);
    __ASSERT_NO_MSG(last_report.d_x == (scroll ? 0 : 8));
    __ASSERT_NO_MSG(last_report.d_scroll_y == (scroll ? 8 : 0));
    __ASSERT_NO_MSG(last_report.d_y == 0 && last_report.d_scroll_x == 0);
    int64_t recovered = last_report_at;
    process(axis, 0);
    k_sleep(K_MSEC(130));
    __ASSERT_NO_MSG(atomic_get(&reports) == 3 && !snapshot(scroll).report_pending);
    __ASSERT_NO_MSG(!snapshot(scroll).active && atomic_get(&endpoint_failures_left) == 0);
    __ASSERT_NO_MSG(body->d_x == 0 && body->d_y == 0 && body->d_scroll_x == 0 &&
                    body->d_scroll_y == 0);
    __ASSERT_NO_MSG(inertia_set_params(inertia, &original) == 0);
    printk("PASS endpoint-error-recovery axis=%u failures=2 released=%lld second=%lld "
           "recovered=%lld time=%lld\n",
           axis, released, second, recovered, k_uptime_get());
}

static void lifecycle_endpoint(enum zmk_transport transport, int profile, bool notify) {
    atomic_set(&endpoint_target, transport | (profile << 2));
    if (notify) {
        raise_zmk_endpoint_changed((struct zmk_endpoint_changed){
            .endpoint = __wrap_zmk_endpoint_get_selected(),
        });
    }
}

static void lifecycle_activity(enum zmk_activity_state state) {
    raise_zmk_activity_state_changed((struct zmk_activity_state_changed){.state = state});
}

static void lifecycle_require_stopped(const struct device *dev) {
    for (unsigned stream = 0; stream < MAX(DT_NUM_INST_STATUS_OKAY(zmk_input_listener), 1);
         stream++) {
        for (unsigned scroll = 0; scroll < 2; scroll++) {
            struct inertia_test_snapshot state;
            __ASSERT_NO_MSG(inertia_test_get_snapshot(dev, stream, scroll, &state) == 0);
            __ASSERT(!state.active && !state.inertial && !state.frame_open && !state.velocity[0] &&
                         !state.velocity[1] && !state.ema[0] && !state.ema[1],
                     "Lifecycle transition left an old stream active");
        }
    }
}

static void lifecycle_wait_pending(const struct device *dev, unsigned stream, bool scroll) {
    int64_t deadline = k_uptime_get() + 1000;
    for (;;) {
        struct inertia_test_snapshot state;
        __ASSERT_NO_MSG(inertia_test_get_snapshot(dev, stream, scroll, &state) == 0);
        if (state.report_pending) {
            return;
        }
        __ASSERT(k_uptime_get() < deadline, "Lifecycle fixture did not queue its production token");
        k_sleep(K_MSEC(1));
    }
}

static void run_endpoint_lifecycle(bool scroll, unsigned phase, bool notify) {
    axis = scroll ? INPUT_REL_WHEEL : INPUT_REL_X;
    atomic_set(&completed, 0);
    atomic_set(&reports, 0);
    atomic_set(&hold_send, phase == 2);
    k_sem_reset(&lifecycle_report_ready);
    lifecycle_endpoint(ZMK_TRANSPORT_USB, 0, true);
    lifecycle_activity(ZMK_ACTIVITY_ACTIVE);
    if (phase == 1) {
        __ASSERT_NO_MSG(input_report(gate_device, INPUT_EV_MSC, 0, 0, false, K_NO_WAIT) == 0);
        __ASSERT_NO_MSG(k_sem_take(&input_entered, K_SECONDS(1)) == 0);
    }
    process(axis, scroll ? 40 : 200);
    if (phase == 1) {
        lifecycle_wait_pending(inertia, 0, scroll);
    } else if (phase == 2) {
        __ASSERT_NO_MSG(k_sem_take(&send_entered, K_SECONDS(1)) == 0);
        __ASSERT_NO_MSG(atomic_get(&reports) == 1 && last_endpoint_target == ZMK_TRANSPORT_USB);
    }
    struct inertia_test_snapshot before = snapshot(scroll);
    int64_t began = k_uptime_get();
    lifecycle_endpoint(ZMK_TRANSPORT_BLE, 1, notify);
    __ASSERT(k_uptime_get() - began <= 1, "Endpoint cancellation waited for transport");
    if (notify) {
        for (size_t i = 0; i < ARRAY_SIZE(lifecycle_nodes); i++) {
            lifecycle_require_stopped(lifecycle_nodes[i]);
        }
        __ASSERT_NO_MSG(snapshot(scroll).generation != before.generation);
    }
    if (phase == 1) {
        __ASSERT_NO_MSG(snapshot(scroll).report_pending); /* Old slot remains owned. */
        k_sem_give(&input_release);
    } else if (phase == 2) {
        __ASSERT_NO_MSG(snapshot(scroll).report_pending);
        k_sem_give(&send_release);
    }
    k_sleep(K_MSEC(130));
    __ASSERT(atomic_get(&reports) == (phase == 2 ? 1 : 0),
             "Old endpoint inertia reached the replacement target");
    lifecycle_require_stopped(inertia);
    __ASSERT_NO_MSG(!snapshot(scroll).report_pending);

    /* Fresh manual input belongs to the new peer and remains functional. */
    const int expected = atomic_get(&reports) + 1;
    k_sem_reset(&lifecycle_report_ready);
    process(axis, scroll ? 40 : 200);
    __ASSERT_NO_MSG(k_sem_take(&lifecycle_report_ready, K_SECONDS(1)) == 0);
    __ASSERT_NO_MSG(atomic_get(&reports) == expected);
    __ASSERT_NO_MSG(last_endpoint_target == (ZMK_TRANSPORT_BLE | (1 << 2)));
    process(axis, 0);
    k_sleep(K_MSEC(130));
    __ASSERT_NO_MSG(atomic_get(&reports) == expected && !snapshot(scroll).report_pending);
    printk("PASS endpoint-lifecycle scroll=%d phase=%u notify=%d time=%lld\n", scroll, phase,
           notify, k_uptime_get());
}

static void run_activity_lifecycle(enum zmk_activity_state activity) {
    axis = INPUT_REL_X;
    atomic_set(&completed, 0);
    atomic_set(&reports, 0);
    lifecycle_endpoint(ZMK_TRANSPORT_USB, 0, true);
    lifecycle_activity(ZMK_ACTIVITY_ACTIVE);
    __ASSERT_NO_MSG(input_report(gate_device, INPUT_EV_MSC, 0, 0, false, K_NO_WAIT) == 0);
    __ASSERT_NO_MSG(k_sem_take(&input_entered, K_SECONDS(1)) == 0);
    process(INPUT_REL_X, 200);
    struct input_event event = {
        .type = INPUT_EV_REL, .code = INPUT_REL_WHEEL, .value = 40, .sync = true};
    struct zmk_input_processor_state context = {.input_device_index = 1};
    for (size_t i = 0; i < ARRAY_SIZE(lifecycle_nodes); i++) {
        if (lifecycle_nodes[i] != inertia) {
            __ASSERT_NO_MSG(zmk_input_processor_handle_event(lifecycle_nodes[i], &event, 0, 0,
                                                             &context) == ZMK_INPUT_PROC_CONTINUE);
        }
    }
    lifecycle_wait_pending(inertia, 0, false);
    for (size_t i = 0; i < ARRAY_SIZE(lifecycle_nodes); i++) {
        if (lifecycle_nodes[i] != inertia) {
            lifecycle_wait_pending(lifecycle_nodes[i], 1, true);
        }
    }
    atomic_val_t tokens_before = atomic_get(&lifecycle_tokens);
    lifecycle_activity(activity);
    for (size_t i = 0; i < ARRAY_SIZE(lifecycle_nodes); i++) {
        lifecycle_require_stopped(lifecycle_nodes[i]);
    }
    __ASSERT_NO_MSG(snapshot(false).report_pending);
    k_sem_give(&input_release);
    k_sleep(K_MSEC(130));
    __ASSERT(atomic_get(&reports) == 0, "Activity transition admitted an old glide");
    __ASSERT_NO_MSG(!snapshot(false).report_pending);
    __ASSERT(atomic_get(&lifecycle_tokens) >=
                 tokens_before + (atomic_val_t)ARRAY_SIZE(lifecycle_nodes),
             "The actual NULL-device callback did not observe retired MSC tokens");
    lifecycle_activity(ZMK_ACTIVITY_ACTIVE);
    printk("PASS activity-lifecycle state=%d all-nodes stale-tokens-visible time=%lld\n", activity,
           k_uptime_get());
}

static void run_lifecycle_noop(void) {
    axis = INPUT_REL_X;
    atomic_set(&completed, 0);
    atomic_set(&reports, 0);
    lifecycle_endpoint(ZMK_TRANSPORT_USB, 0, true);
    lifecycle_activity(ZMK_ACTIVITY_ACTIVE);
    process(axis, 200);
    struct inertia_test_snapshot before = snapshot(false);
    lifecycle_endpoint(ZMK_TRANSPORT_USB, 7, true); /* USB identity ignores BLE storage. */
    lifecycle_activity(ZMK_ACTIVITY_ACTIVE);
    __ASSERT(snapshot(false).generation == before.generation && snapshot(false).active,
             "Initial or no-op lifecycle notification cancelled fresh motion");
    lifecycle_endpoint(ZMK_TRANSPORT_BLE, 0, true);
    process(axis, 200);
    before = snapshot(false);
    lifecycle_endpoint(ZMK_TRANSPORT_BLE, 0, true);
    __ASSERT_NO_MSG(snapshot(false).generation == before.generation && snapshot(false).active);
    lifecycle_endpoint(ZMK_TRANSPORT_BLE, 1, true);
    lifecycle_require_stopped(inertia);
    __ASSERT_NO_MSG(snapshot(false).generation != before.generation);
    process(axis, 200);
    lifecycle_endpoint(ZMK_TRANSPORT_NONE, 0, true);
    lifecycle_require_stopped(inertia);
    k_sleep(K_MSEC(130));
    __ASSERT_NO_MSG(atomic_get(&reports) == 0);
    printk("PASS lifecycle-noop initial repeat profile-switch disconnect time=%lld\n",
           k_uptime_get());
}

static void run_tests(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    __ASSERT_NO_MSG(device_is_ready(inertia));
    const char *focused = getenv("INERTIA_LIFECYCLE_CASE");
    if (focused != NULL) {
        lifecycle_running = true;
        if (!strcmp(focused, "endpoint-queued-move")) {
            run_endpoint_lifecycle(false, 1, true);
        } else if (!strcmp(focused, "endpoint-queued-scroll")) {
            run_endpoint_lifecycle(true, 1, true);
        } else if (!strcmp(focused, "endpoint-observed-move")) {
            run_endpoint_lifecycle(false, 1, false);
        } else if (!strcmp(focused, "endpoint-observed-scroll")) {
            run_endpoint_lifecycle(true, 1, false);
        } else if (!strcmp(focused, "activity-idle")) {
            run_activity_lifecycle(ZMK_ACTIVITY_IDLE);
        } else if (!strcmp(focused, "activity-sleep")) {
            run_activity_lifecycle(ZMK_ACTIVITY_SLEEP);
        } else if (!strcmp(focused, "lifecycle-noop")) {
            run_lifecycle_noop();
        } else {
            __ASSERT(false, "Unknown lifecycle scenario: %s", focused);
        }
        printk("inertia lifecycle focused: PASS case=%s\n", focused);
        exit(0);
    }
    run_endpoint_error_recovery(false);
    run_endpoint_error_recovery(true);
    run_timeout_conversion();
    run_hid_handoff_period(false);
    run_hid_handoff_period(true);
    run_hid_handoff_rearm(false, false);
    run_hid_handoff_rearm(false, true);
    run_hid_handoff_rearm(true, false);
    run_hid_handoff_rearm(true, true);
    run_delayed_consumer_period(false);
    run_delayed_consumer_period(true);
    run_rearm_expired_before_exit(false);
    run_rearm_expired_before_exit(true);
    run_running_rearm(false, false);
    run_running_rearm(true, false);
    run_running_rearm(false, true);
    run_running_rearm(true, true);
    run_case(false, false);
    run_case(true, false);
    run_case(false, true);
    run_case(true, true);
    run_queued_cancel(false);
    run_queued_cancel(true);
    run_independent_node();
    run_queue_full();
    run_settings_cancel(false, false);
    run_settings_cancel(true, false);
    run_settings_cancel(false, true);
    run_settings_cancel(true, true);
    printk("inertia endpoint boundary: PASS groups=29\n");
    lifecycle_running = true;
    for (unsigned scroll = 0; scroll < 2; scroll++) {
        for (unsigned phase = 0; phase < 3; phase++) {
            run_endpoint_lifecycle(scroll, phase, true);
        }
        run_endpoint_lifecycle(scroll, 1, false);
    }
    run_activity_lifecycle(ZMK_ACTIVITY_IDLE);
    run_activity_lifecycle(ZMK_ACTIVITY_SLEEP);
    run_lifecycle_noop();
    printk("inertia lifecycle boundary: PASS groups=11\n");
    exit(0);
}

K_THREAD_DEFINE(boundary_test, 4096, run_tests, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO,
                0, 100);
