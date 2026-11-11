/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <drivers/input_processor.h>
#include <zmk-input-inertia/inertia_test.h>
#include <zmk-input-inertia/runtime.h>

static const struct device *const inertia = DEVICE_DT_GET(DT_NODELABEL(inertia_runtime_test));
enum {
    SOURCE_CAPACITY = CONFIG_ZMK_INPUT_INERTIA_MAX_SOURCES != 0
                          ? CONFIG_ZMK_INPUT_INERTIA_MAX_SOURCES
                          : MAX(DT_NUM_INST_STATUS_OKAY(zmk_input_listener), 1),
};

static void check_sources(void *a, void *b, void *c) {
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    struct inertia_test_snapshot first, last;
    __ASSERT_NO_MSG(inertia_test_get_snapshot(inertia, SOURCE_CAPACITY - 1, false, &last) == 0);
    __ASSERT_NO_MSG(inertia_test_get_snapshot(inertia, SOURCE_CAPACITY, false, &last) == -EINVAL);
    struct input_event event = {.type = INPUT_EV_REL, .code = INPUT_REL_X,
                                .value = 200, .sync = true};
    __ASSERT_NO_MSG(zmk_input_processor_handle_event(inertia, &event, 0, 0, NULL) == 0);
    __ASSERT_NO_MSG(event.type == INPUT_EV_REL && event.code == INPUT_REL_X &&
                    event.value == 200 && event.sync);
    __ASSERT_NO_MSG(inertia_test_get_snapshot(inertia, 0, false, &first) == 0);
    __ASSERT_NO_MSG(first.active && first.velocity[0] == 200 && first.velocity[1] == 0);
    if (SOURCE_CAPACITY < 256) {
        struct zmk_input_processor_state invalid = {.input_device_index = (uint8_t)SOURCE_CAPACITY};
        event.value = -123;
        __ASSERT_NO_MSG(zmk_input_processor_handle_event(inertia, &event, 0, 0, &invalid) == 0);
        __ASSERT_NO_MSG(event.value == -123 && event.sync);
        __ASSERT_NO_MSG(inertia_test_get_snapshot(inertia, 0, false, &last) == 0);
        __ASSERT_NO_MSG(last.generation == first.generation && last.velocity[0] == 200 &&
                        last.velocity[1] == 0 && last.active);
    }
    struct zmk_input_processor_state boundary = {.input_device_index = (uint8_t)(SOURCE_CAPACITY - 1)};
    event.value = -80;
    __ASSERT_NO_MSG(zmk_input_processor_handle_event(inertia, &event, 0, 0, &boundary) == 0);
    __ASSERT_NO_MSG(inertia_test_get_snapshot(inertia, SOURCE_CAPACITY - 1, false, &last) == 0);
    __ASSERT_NO_MSG(last.active && last.velocity[0] == -80 && last.velocity[1] == 0);
    if (SOURCE_CAPACITY > 1) {
        __ASSERT_NO_MSG(inertia_test_get_snapshot(inertia, 0, false, &first) == 0);
        __ASSERT_NO_MSG(!first.active && first.velocity[0] == 0); /* Shared-node cancellation. */
    }
    struct inertia_params params;
    __ASSERT_NO_MSG(inertia_get_params(inertia, &params) == 0);
    params.trigger_ms++;
    __ASSERT_NO_MSG(inertia_set_params(inertia, &params) == 0);
    __ASSERT_NO_MSG(inertia_test_get_snapshot(inertia, SOURCE_CAPACITY - 1, false, &last) == 0);
    __ASSERT_NO_MSG(!last.active && !last.frame_open && !last.report_pending);
    printk("inertia source limit: PASS capacity=%u listeners=%u NULL=0 boundary=%u\n",
           SOURCE_CAPACITY, DT_NUM_INST_STATUS_OKAY(zmk_input_listener), SOURCE_CAPACITY - 1);
    exit(0);
}
K_THREAD_DEFINE(source_limit_review, 2048, check_sources, NULL, NULL, NULL, 0, 0, 1);
