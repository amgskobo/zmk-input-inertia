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
#include <zmk-input-inertia/runtime.h>
#if IS_ENABLED(CONFIG_ZMK_INPUT_INERTIA_CUSTOM_SETTINGS)
#include <cormoran/zmk/custom_settings.h>
#include <zmk-input-inertia/custom_settings.h>
#endif

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

static void test_runtime_params(void) {
    struct inertia_params params, after;
    __ASSERT_NO_MSG(inertia_get_params(inertia,&params)==0);
    after=params;after.move_threshold_stop=after.move_threshold_start;
    __ASSERT_NO_MSG(inertia_set_params(inertia,&after)==-EINVAL);
    struct input_event event=relative_event(INPUT_REL_X,200,true);process(0,&event);
    uint32_t generation=snapshot(false).generation;
    __ASSERT_NO_MSG(inertia_set_params(inertia,&params)==0);
    __ASSERT_NO_MSG(snapshot(false).active && snapshot(false).generation==generation);
    after=params;after.trigger_ms++;
    __ASSERT_NO_MSG(inertia_set_params(inertia,&after)==0);
    __ASSERT_NO_MSG(!snapshot(false).active && snapshot(false).generation!=generation);
    __ASSERT_NO_MSG(inertia_set_params(inertia,&params)==0);
    printk("PASS: live params reject invalid thresholds, preserve no-op and cancel on change\n");
}
#if IS_ENABLED(CONFIG_ZMK_INPUT_INERTIA_CUSTOM_SETTINGS)
static const struct zmk_custom_setting *trigger_setting(void) {
    const struct zmk_custom_setting *s=zmk_custom_setting_find(ZMK_INPUT_INERTIA_SUBSYSTEM,"glide_runtime.trigger_ms");
    __ASSERT_NO_MSG(s!=NULL);return s;
}
static const struct zmk_custom_setting *period_setting(const char *key) {
    const struct zmk_custom_setting *s=zmk_custom_setting_find("amgskobo__inertia",key);
    __ASSERT_NO_MSG(s!=NULL);return s;
}
static bool verify_reload(void) {
    struct zmk_custom_setting_value value;
    __ASSERT_NO_MSG(zmk_custom_setting_find("amgskobo__in","glide_runtime.trigger_ms")==NULL);
    __ASSERT_NO_MSG(zmk_custom_setting_find("amgskobo__inertia","glide_runtime.move_interval")==NULL);
    __ASSERT_NO_MSG(zmk_custom_setting_find("amgskobo__inertia","glide_runtime.scroll_interval")==NULL);
    __ASSERT_NO_MSG(zmk_custom_setting_read(trigger_setting(),&value)==0);
    if(value.int32_value!=37){__ASSERT_NO_MSG(value.int32_value==20);return false;}
    struct inertia_params params;
    __ASSERT_NO_MSG(inertia_get_params(inertia,&params)==0 && params.trigger_ms==37);
    __ASSERT_NO_MSG(params.move_report_interval_ms==43 && params.scroll_report_interval_ms==71);
    __ASSERT_NO_MSG(zmk_custom_setting_read(period_setting("glide_runtime.move_period"),&value)==0 && value.int32_value==43);
    __ASSERT_NO_MSG(zmk_custom_setting_read(period_setting("glide_runtime.scroll_period"),&value)==0 && value.int32_value==71);
    const struct device *other=DEVICE_DT_GET(DT_NODELABEL(independent_runtime));
    __ASSERT_NO_MSG(inertia_get_params(other,&params)==0 && params.trigger_ms==35);
    printk("PASS: persisted Inertia value reloads through actual registry and driver\n");return true;
}
static void test_registry(void) {
    const struct zmk_custom_setting *s=trigger_setting();
    struct zmk_custom_setting_value value=ZMK_CUSTOM_SETTING_VALUE_INT32(0);
    __ASSERT_NO_MSG(zmk_custom_setting_write(s,&value,ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY)!=0);
    value=(struct zmk_custom_setting_value)ZMK_CUSTOM_SETTING_VALUE_BOOL(true);
    __ASSERT_NO_MSG(zmk_custom_setting_write(s,&value,ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY)!=0);
    value=(struct zmk_custom_setting_value)ZMK_CUSTOM_SETTING_VALUE_INT32(37);
    __ASSERT_NO_MSG(zmk_custom_setting_write(s,&value,ZMK_CUSTOM_SETTING_WRITE_MODE_PERSIST)==0);
    value=(struct zmk_custom_setting_value)ZMK_CUSTOM_SETTING_VALUE_INT32(43);
    __ASSERT_NO_MSG(zmk_custom_setting_write(period_setting("glide_runtime.move_period"),&value,ZMK_CUSTOM_SETTING_WRITE_MODE_PERSIST)==0);
    value=(struct zmk_custom_setting_value)ZMK_CUSTOM_SETTING_VALUE_INT32(71);
    __ASSERT_NO_MSG(zmk_custom_setting_write(period_setting("glide_runtime.scroll_period"),&value,ZMK_CUSTOM_SETTING_WRITE_MODE_PERSIST)==0);
    struct inertia_params params;
    __ASSERT_NO_MSG(inertia_get_params(inertia,&params)==0 && params.trigger_ms==37);
    const struct device *other=DEVICE_DT_GET(DT_NODELABEL(independent_runtime));
    __ASSERT_NO_MSG(inertia_get_params(other,&params)==0 && params.trigger_ms==35);
    printk("PASS: Inertia registry range/type rejection, per-node apply and NVS save\n");
}
#endif

static void run_tests(void *p1, void *p2, void *p3) {
#if IS_ENABLED(CONFIG_ZMK_INPUT_INERTIA_CUSTOM_SETTINGS)
    if(verify_reload()){printk("inertia runtime tests: PASS\n");exit(0);}
#endif

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

    test_runtime_params();
#if IS_ENABLED(CONFIG_ZMK_INPUT_INERTIA_CUSTOM_SETTINGS)
    test_registry();
#endif
    printk("inertia runtime tests: PASS\n");
    exit(0);
}

K_THREAD_DEFINE(inertia_runtime_tests, 4096, run_tests, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 100);
