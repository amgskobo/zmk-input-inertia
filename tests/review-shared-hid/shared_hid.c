/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT zmk_input_listener
#include <stdlib.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <drivers/input_processor.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>
#include <zmk-input-inertia/inertia_test.h>

static const struct device *const inertia = DEVICE_DT_GET(DT_NODELABEL(inertia_runtime_test));
static const struct device *const pointer = DEVICE_DT_GET(DT_NODELABEL(kscan));
DEVICE_DT_DEFINE(DT_NODELABEL(manual_pointer), NULL, NULL, NULL, NULL,
                 POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, NULL);
static const struct device *const manual = DEVICE_DT_GET(DT_NODELABEL(manual_pointer));
BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(zmk_input_listener) >= 2);
#define MANUAL_INDEX(n) + (DT_SAME_NODE(DT_DRV_INST(n), \
                            DT_NODELABEL(review_manual_listener)) ? (n) : 0)
enum { MANUAL_STREAM = 0 DT_INST_FOREACH_STATUS_OKAY(MANUAL_INDEX),
       OLD_STREAM = MANUAL_STREAM == 0 ? 1 : 0 };
static K_SEM_DEFINE(input_blocked, 0, 1);
static K_SEM_DEFINE(resume_input, 0, 1);
static K_SEM_DEFINE(manual_sent, 0, 1);

static void input_gate(struct input_event *event, void *user_data) {
    ARG_UNUSED(user_data);
    if (event->type == INPUT_EV_MSC && event->code == 0x7fff) {
        k_sem_give(&input_blocked);
        __ASSERT_NO_MSG(k_sem_take(&resume_input, K_SECONDS(1)) == 0);
    }
}
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(manual_pointer)), input_gate, NULL);

static K_SEM_DEFINE(entered, 0, 1);
static K_SEM_DEFINE(resume_send, 0, 1);
static K_SEM_DEFINE(other_sent, 0, 1);
static atomic_t hold_send = ATOMIC_INIT(1);
static atomic_t report_count;
static struct zmk_hid_mouse_report_body reports[3];

int __wrap_zmk_endpoint_send_mouse_report(void);
int __wrap_zmk_endpoint_send_mouse_report(void) {
    int index = atomic_inc(&report_count);
    __ASSERT_NO_MSG(index < 3);
    reports[index] = zmk_hid_get_mouse_report()->body;
    if (atomic_cas(&hold_send, 1, 0)) {
        __ASSERT_NO_MSG(reports[index].d_x == 90 && reports[index].d_y == 0);
        k_sem_give(&entered);
        __ASSERT_NO_MSG(k_sem_take(&resume_send, K_SECONDS(1)) == 0);
        __ASSERT(memcmp(&reports[index], &zmk_hid_get_mouse_report()->body,
                        sizeof(reports[index])) == 0,
                 "pending inertia report overwritten by another HID producer");
    } else if (index == 1) {
        __ASSERT_NO_MSG(reports[index].d_x == -7 && reports[index].d_y == 3);
        k_sem_give(&other_sent);
    } else {
        __ASSERT(reports[index].d_x == 3 && reports[index].d_y == -2,
                 "stale inertia reached output before manual frame");
        k_sem_give(&manual_sent);
    }
    return 0;
}

static void review(void *a, void *b, void *c) {
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    struct input_event event = {.type = INPUT_EV_REL, .code = INPUT_REL_X,
                                .value = 200, .sync = true};
    struct zmk_input_processor_state state = {.input_device_index = 0};
    __ASSERT_NO_MSG(zmk_input_processor_handle_event(inertia, &event, 0, 0, &state) ==
                    ZMK_INPUT_PROC_CONTINUE);
    __ASSERT_NO_MSG(k_sem_take(&entered, K_SECONDS(1)) == 0);
    /* The competing producer uses the actual input queue and unmodified ZMK
     * input listener. The existing mock device supplies identity, not a model
     * of sensor hardware. No direct test-side HID mutation replaces the listener. */
    __ASSERT_NO_MSG(input_report_rel(pointer, INPUT_REL_X, -7, false, K_NO_WAIT) == 0);
    __ASSERT_NO_MSG(input_report_rel(pointer, INPUT_REL_Y, 3, true, K_NO_WAIT) == 0);
    k_sleep(K_MSEC(10));
    printk("held inertia x=%d y=%d; current x=%d y=%d time=%lld\n",
           reports[0].d_x, reports[0].d_y, zmk_hid_get_mouse_report()->body.d_x,
           zmk_hid_get_mouse_report()->body.d_y, k_uptime_get());
    __ASSERT(memcmp(&reports[0], &zmk_hid_get_mouse_report()->body,
                    sizeof(reports[0])) == 0,
             "pending inertia report overwritten by another HID producer");
    k_sem_give(&resume_send);
    __ASSERT_NO_MSG(k_sem_take(&other_sent, K_SECONDS(1)) == 0);
    event.value = 0;
    __ASSERT_NO_MSG(zmk_input_processor_handle_event(inertia, &event, 0, 0, &state) ==
                    ZMK_INPUT_PROC_CONTINUE);
    k_sleep(K_MSEC(130));
    __ASSERT_NO_MSG(atomic_get(&report_count) == 2);
    const struct zmk_hid_mouse_report_body zero = {0};
    __ASSERT_NO_MSG(memcmp(&zero, &zmk_hid_get_mouse_report()->body, sizeof(zero)) == 0);
    /* Hold the actual input consumer. X is before the old inertia token and
     * Y/SYNC after it: cancellation must happen on the first axis, without
     * losing the first manual frame or depending on test-side cancellation. */
    __ASSERT_NO_MSG(input_report(manual, INPUT_EV_MSC, 0x7fff, 0, false, K_NO_WAIT) == 0);
    __ASSERT_NO_MSG(k_sem_take(&input_blocked, K_SECONDS(1)) == 0);
    state.input_device_index = OLD_STREAM;
    event.value = 200;
    __ASSERT_NO_MSG(zmk_input_processor_handle_event(inertia, &event, 0, 0, &state) ==
                    ZMK_INPUT_PROC_CONTINUE);
    __ASSERT_NO_MSG(input_report_rel(manual, INPUT_REL_X, 3, false, K_NO_WAIT) == 0);
    struct inertia_test_snapshot snapshot = {0};
    int64_t deadline = k_uptime_get() + 1000;
    do {
        __ASSERT_NO_MSG(inertia_test_get_snapshot(inertia, OLD_STREAM, false,
                                                 &snapshot) == 0);
        if (snapshot.report_pending) { break; }
        __ASSERT_NO_MSG(k_uptime_get() < deadline);
        k_sleep(K_MSEC(1));
    } while (true);
    __ASSERT_NO_MSG(input_report_rel(manual, INPUT_REL_Y, -2, true, K_NO_WAIT) == 0);
    k_sem_give(&resume_input);
    __ASSERT_NO_MSG(k_sem_take(&manual_sent, K_SECONDS(1)) == 0);
    k_sleep(K_MSEC(130));
    __ASSERT_NO_MSG(atomic_get(&report_count) == 3);
    __ASSERT_NO_MSG(memcmp(&zero, &zmk_hid_get_mouse_report()->body, sizeof(zero)) == 0);
    __ASSERT_NO_MSG(inertia_test_get_snapshot(inertia, OLD_STREAM, false,
                                             &snapshot) == 0);
    __ASSERT_NO_MSG(!snapshot.active && !snapshot.report_pending);
    printk("inertia shared HID regression: PASS ordered_reports=3 real_manual_preemption=1\n");
    exit(0);
}
K_THREAD_DEFINE(shared_hid_review, 4096, review, NULL, NULL, NULL, 0, 0, 100);
