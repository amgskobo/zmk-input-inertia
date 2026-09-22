/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include <drivers/input_processor.h>
#include <dt-bindings/zmk/keys.h>
#include <zmk/hid.h>

#include <zmk-input-inertia/inertia_test.h>

static const struct device *const inertia = DEVICE_DT_GET(DT_NODELABEL(inertia_runtime_test));
static atomic_t cancel_before_emit;
static atomic_t hook_calls;

static void process(uint8_t stream_index, struct input_event *event) {
    struct zmk_input_processor_state state = {
        .input_device_index = stream_index,
        .remainder = NULL,
    };

    __ASSERT_NO_MSG(zmk_input_processor_handle_event(inertia, event, 0, 0, &state) ==
                    ZMK_INPUT_PROC_CONTINUE);
}

static struct input_event relative_event(uint16_t code, int32_t value, bool sync) {
    return (struct input_event){
        .type = INPUT_EV_REL,
        .code = code,
        .value = value,
        .sync = sync,
    };
}

void inertia_test_before_emit(const struct device *dev, size_t stream_index, bool scroll) {
    __ASSERT_NO_MSG(dev == inertia);
    atomic_inc(&hook_calls);

    if (!scroll && atomic_cas(&cancel_before_emit, 1, 0)) {
        struct input_event cancel = relative_event(INPUT_REL_X, 0, true);

        process((uint8_t)stream_index, &cancel);
    }
}

static struct inertia_test_snapshot snapshot_at(size_t stream_index, bool scroll) {
    struct inertia_test_snapshot value;

    __ASSERT_NO_MSG(inertia_test_get_snapshot(inertia, stream_index, scroll, &value) == 0);
    return value;
}

static struct inertia_test_snapshot snapshot(bool scroll) { return snapshot_at(0, scroll); }

static void run_tests(void *p1, void *p2, void *p3) {
    struct inertia_test_snapshot state;
    struct input_event event;

    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    __ASSERT_NO_MSG(device_is_ready(inertia));
    __ASSERT_NO_MSG(inertia_test_get_snapshot(NULL, 0, false, &state) == -EINVAL);
    __ASSERT_NO_MSG(inertia_test_get_snapshot(inertia, 0, false, NULL) == -EINVAL);
    __ASSERT_NO_MSG(inertia_test_get_snapshot(inertia, SIZE_MAX, false, &state) == -EINVAL);

    /* An index past the listeners passes through and never lands on stream zero. */
    event = relative_event(INPUT_REL_X, 200, true);
    process(UINT8_MAX, &event);
    __ASSERT_NO_MSG(event.value == 200);
    state = snapshot(false);
    __ASSERT_NO_MSG(!state.frame_open);
    __ASSERT_NO_MSG(!state.active);
    __ASSERT_NO_MSG(state.velocity[INERTIA_AXIS_X] == 0);
    __ASSERT_NO_MSG(state.generation == 0U);

#if DT_NUM_INST_STATUS_OKAY(zmk_input_listener) > 1
    event = relative_event(INPUT_REL_X, 200, false);
    process(0, &event);
    event = relative_event(INPUT_REL_Y, 80, true);
    process(1, &event);
    state = snapshot_at(0, false);
    __ASSERT_NO_MSG(!state.frame_open);
    __ASSERT_NO_MSG(!state.active);
    state = snapshot_at(1, false);
    __ASSERT_NO_MSG(state.velocity[INERTIA_AXIS_X] == 0);
    __ASSERT_NO_MSG(state.velocity[INERTIA_AXIS_Y] == 80);
    event = relative_event(INPUT_REL_Y, 0, true);
    process(1, &event);
#endif

    event = relative_event(INPUT_REL_X, 200, false);
    process(0, &event);
    state = snapshot(false);
    __ASSERT_NO_MSG(state.frame_open);
    __ASSERT_NO_MSG(!state.active);

    event = (struct input_event){
        .type = INPUT_EV_KEY,
        .code = INPUT_KEY_0,
        .value = 0,
        .sync = true,
    };
    process(0, &event);
    state = snapshot(false);
    __ASSERT_NO_MSG(!state.frame_open);
    __ASSERT_NO_MSG(state.active);
    __ASSERT_NO_MSG(state.velocity[INERTIA_AXIS_X] == 200);
    __ASSERT_NO_MSG(state.velocity[INERTIA_AXIS_Y] == 0);
    __ASSERT_NO_MSG(state.ema[INERTIA_AXIS_X] == 100);

    event = relative_event(INPUT_REL_Y, 80, true);
    process(0, &event);
    state = snapshot(false);
    __ASSERT_NO_MSG(state.velocity[INERTIA_AXIS_X] == 0);
    __ASSERT_NO_MSG(state.velocity[INERTIA_AXIS_Y] == 80);
    __ASSERT_NO_MSG(state.ema[INERTIA_AXIS_X] == 50);
    __ASSERT_NO_MSG(state.ema[INERTIA_AXIS_Y] == 40);

    event = relative_event(INPUT_REL_X, 0, true);
    process(0, &event);
    state = snapshot(false);
    __ASSERT_NO_MSG(!state.active);
    __ASSERT_NO_MSG(state.ema[INERTIA_AXIS_X] == 0);
    __ASSERT_NO_MSG(state.ema[INERTIA_AXIS_Y] == 0);

    atomic_set(&cancel_before_emit, 1);
    event = relative_event(INPUT_REL_X, 200, true);
    process(0, &event);
    k_sleep(K_MSEC(50));
    state = snapshot(false);
    __ASSERT_NO_MSG(atomic_get(&cancel_before_emit) == 0);
    __ASSERT_NO_MSG(atomic_get(&hook_calls) >= 1);
    __ASSERT_NO_MSG(state.emit_count == 0U);
    __ASSERT_NO_MSG(!state.active);

    event = relative_event(INPUT_REL_X, 200, true);
    process(0, &event);
    k_sleep(K_MSEC(50));
    state = snapshot(false);
    __ASSERT_NO_MSG(state.emit_count == 1U);
    __ASSERT_NO_MSG(state.active);
    __ASSERT_NO_MSG(state.inertial);

    event = relative_event(INPUT_REL_X, 0, true);
    process(0, &event);

#if DT_NUM_INST_STATUS_OKAY(zmk_input_listener) > 1
    event = relative_event(INPUT_REL_WHEEL, 20, true);
    process(1, &event);
    state = snapshot_at(1, true);
    __ASSERT_NO_MSG(state.active);
#endif

    __ASSERT_NO_MSG(zmk_hid_keyboard_press(HID_USAGE_KEY_KEYBOARD_LEFTCONTROL) >= 0);
    event = relative_event(INPUT_REL_WHEEL, 20, true);
    process(0, &event);
    state = snapshot(true);
    __ASSERT_NO_MSG(!state.active);
    __ASSERT_NO_MSG(state.emit_count == 0U);
#if DT_NUM_INST_STATUS_OKAY(zmk_input_listener) > 1
    state = snapshot_at(1, true);
    __ASSERT_NO_MSG(!state.active);
    __ASSERT_NO_MSG(state.emit_count == 0U);
#endif
    __ASSERT_NO_MSG(zmk_hid_keyboard_release(HID_USAGE_KEY_KEYBOARD_LEFTCONTROL) >= 0);

    printk("inertia runtime tests: PASS\n");
    exit(0);
}

K_THREAD_DEFINE(inertia_runtime_tests, 4096, run_tests, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 100);
