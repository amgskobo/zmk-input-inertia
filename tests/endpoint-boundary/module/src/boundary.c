/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include <drivers/input_processor.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>
#include <zmk-input-inertia/inertia_test.h>
#include <zmk-input-inertia/runtime.h>

static const struct device *const inertia = DEVICE_DT_GET(DT_NODELABEL(inertia_runtime_test));
static const struct device *const independent = DEVICE_DT_GET(DT_NODELABEL(inertia_independent));
static const struct device *const gate_device = DEVICE_DT_GET(DT_NODELABEL(kscan));
static K_SEM_DEFINE(input_entered, 0, 1);
static K_SEM_DEFINE(input_release, 0, 1);
static void block_input(struct input_event *event, void *user_data) {
    ARG_UNUSED(event); ARG_UNUSED(user_data);
    k_sem_give(&input_entered);
    __ASSERT_NO_MSG(k_sem_take(&input_release, K_SECONDS(2)) == 0);
}
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(kscan)), block_input, NULL);
static K_SEM_DEFINE(send_entered, 0, 1);
static K_SEM_DEFINE(send_release, 0, 1);
static K_SEM_DEFINE(cancel_start, 0, 1);
static K_SEM_DEFINE(cancel_done, 0, 1);
static atomic_t hold_send, cancel_in_hook, cancelling, completed, reports;
static uint16_t axis;
static int64_t cancel_begin, cancel_end;

static void process(uint16_t code, int32_t value) {
    struct input_event event = {.type = INPUT_EV_REL, .code = code, .value = value, .sync = true};
    struct zmk_input_processor_state state = {.input_device_index = 0};
    __ASSERT_NO_MSG(zmk_input_processor_handle_event(inertia, &event, 0, 0, &state) ==
                    ZMK_INPUT_PROC_CONTINUE);
}

void inertia_test_before_emit(const struct device *dev, size_t stream, bool scroll) {
    __ASSERT_NO_MSG(dev == inertia || dev == independent);
    __ASSERT_NO_MSG(stream < 2);
    if (dev == independent) { return; }
    if (atomic_cas(&cancel_in_hook, 1, 0)) {
        process(scroll ? INPUT_REL_WHEEL : INPUT_REL_X, 0);
    }
}

int __wrap_zmk_endpoint_send_mouse_report(void);
int __wrap_zmk_endpoint_send_mouse_report(void) {
    /* Substitute only the transport boundary. The complete production driver,
     * actual ZMK HID storage and real kernel mutex/work/thread APIs are used. */
    __ASSERT_NO_MSG(!atomic_get(&completed));
    const struct zmk_hid_mouse_report_body *body = &zmk_hid_get_mouse_report()->body;
    __ASSERT_NO_MSG(axis == INPUT_REL_X ? body->d_x != 0 : body->d_scroll_y != 0);
    atomic_inc(&reports);
    if (atomic_cas(&hold_send, 1, 0)) {
        k_sem_give(&send_entered);
        __ASSERT_NO_MSG(k_sem_take(&send_release, K_SECONDS(1)) == 0);
    }
    /* Acceptance precedes the controlled hold. A submitted report cannot be retracted. */
    printk("REPORT axis=%u x=%d y=%d wheel=%d hwheel=%d time=%lld\n", axis, body->d_x,
           body->d_y, body->d_scroll_y, body->d_scroll_x, k_uptime_get());
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
           "wait_ms=%lld time=%lld\n", axis, waited, cancel_begin, cancel_end,
           cancel_end - cancel_begin, k_uptime_get());
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
    for (int i = 0; i < 60 && !snapshot(scroll).report_pending; i++) { k_sleep(K_MSEC(1)); }
    __ASSERT_NO_MSG(snapshot(scroll).report_pending);
    int16_t saved = snapshot(scroll).velocity[scroll ? INERTIA_AXIS_Y : INERTIA_AXIS_X];
    k_sleep(K_MSEC(130)); /* a late consumer must not accumulate catch-up output */
    __ASSERT_NO_MSG(snapshot(scroll).report_pending);
    __ASSERT_NO_MSG(snapshot(scroll).velocity[scroll ? INERTIA_AXIS_Y : INERTIA_AXIS_X] == saved);
    struct zmk_input_processor_state other = {.input_device_index = 1};
    struct input_event manual = {.type = INPUT_EV_REL, .code = INPUT_REL_X,
                                 .value = 3, .sync = false};
    __ASSERT_NO_MSG(zmk_input_processor_handle_event(inertia, &manual, 0, 0, &other) ==
                    ZMK_INPUT_PROC_CONTINUE);
    __ASSERT_NO_MSG(manual.value == 3 && !manual.sync);
    __ASSERT_NO_MSG(!snapshot(scroll).active); /* stop on first axis, before sync */
    manual.code = INPUT_REL_Y; manual.value = -2; manual.sync = true;
    __ASSERT_NO_MSG(zmk_input_processor_handle_event(inertia, &manual, 0, 0, &other) ==
                    ZMK_INPUT_PROC_CONTINUE);
    __ASSERT_NO_MSG(manual.value == -2);
    k_sem_give(&input_release);
    k_sleep(K_MSEC(130));
    __ASSERT_NO_MSG(!snapshot(scroll).report_pending);
    __ASSERT_NO_MSG(snapshot(scroll).emit_count == before && atomic_get(&reports) == 0);
    printk("PASS queued-cancel axis=%u first_manual_axis_preserved=1 reports=0 time=%lld\n",
           axis, k_uptime_get());
}

static void run_independent_node(void) {
    axis = INPUT_REL_WHEEL;
    atomic_set(&completed, 0);
    atomic_set(&reports, 0);
    struct zmk_input_processor_state state = {.input_device_index = 0};
    struct input_event event = {.type = INPUT_EV_REL, .code = INPUT_REL_WHEEL,
                                .value = 40, .sync = true};
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
    axis=scroll?INPUT_REL_WHEEL:INPUT_REL_X;
    atomic_set(&completed,0);atomic_set(&reports,0);atomic_set(&hold_send,blocked);
    struct inertia_params original, changed, other_before, other_after;
    __ASSERT_NO_MSG(inertia_get_params(inertia,&original)==0);
    __ASSERT_NO_MSG(inertia_get_params(independent,&other_before)==0);
    if(!blocked)gate_start();
    process(axis,scroll?40:200);
    if(blocked) { __ASSERT_NO_MSG(k_sem_take(&send_entered,K_SECONDS(1))==0); }
    else {
        for(int i=0;i<60 && !snapshot(scroll).report_pending;i++)k_sleep(K_MSEC(1));
        __ASSERT_NO_MSG(snapshot(scroll).report_pending);
    }
    changed=original;changed.trigger_ms++;
    int64_t begin=k_uptime_get();
    __ASSERT_NO_MSG(inertia_set_params(inertia,&changed)==0);
    __ASSERT(k_uptime_get()-begin<=1,"blocked endpoint delayed setting application");
    __ASSERT(!snapshot(scroll).active,"settings failed to cancel queued inertia");
    __ASSERT_NO_MSG(inertia_get_params(independent,&other_after)==0 && other_after.trigger_ms==other_before.trigger_ms);
    atomic_set(&completed,1);
    k_sem_give(blocked?&send_release:&input_release);
    k_sleep(K_MSEC(150));
    __ASSERT_NO_MSG(atomic_get(&reports)==(blocked?1:0));
    __ASSERT_NO_MSG(!snapshot(scroll).report_pending);
    const struct zmk_hid_mouse_report_body *body=&zmk_hid_get_mouse_report()->body;
    __ASSERT_NO_MSG(body->d_x==0 && body->d_y==0 && body->d_scroll_x==0 && body->d_scroll_y==0);
    __ASSERT_NO_MSG(inertia_set_params(inertia,&original)==0);
    printk("PASS settings cancel axis=%u blocked=%d reports=%ld time=%lld\n",axis,blocked,(long)atomic_get(&reports),k_uptime_get());
}

static void run_tests(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    __ASSERT_NO_MSG(device_is_ready(inertia));
    run_case(false, false);
    run_case(true, false);
    run_case(false, true);
    run_case(true, true);
    run_queued_cancel(false);
    run_queued_cancel(true);
    run_independent_node();
    run_queue_full();
    run_settings_cancel(false,false);run_settings_cancel(true,false);
    run_settings_cancel(false,true);run_settings_cancel(true,true);
    printk("inertia endpoint boundary: PASS groups=12\n");
    exit(0);
}

K_THREAD_DEFINE(boundary_test, 4096, run_tests, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 100);
