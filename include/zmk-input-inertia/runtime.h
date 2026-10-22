/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
#include <stdbool.h>
struct device;
/* Percentages and milliseconds, matching DTS. Short keys fit Zephyr storage. */
#define INERTIA_INT_FIELDS(F, n) \
    F(n, trigger_ms, "trigger_ms", 1, 65535) \
    F(n, move_decay_factor_int, "move_decay", 0, 99) \
    F(n, move_report_interval_ms, "move_period", 1, 65535) \
    F(n, move_threshold_start, "move_start", 1, 32767) \
    F(n, move_threshold_stop, "move_stop", 0, 32766) \
    F(n, scroll_decay_factor_int, "scroll_decay", 0, 99) \
    F(n, scroll_report_interval_ms, "scroll_period", 1, 65535) \
    F(n, scroll_threshold_start, "scroll_start", 1, 32767) \
    F(n, scroll_threshold_stop, "scroll_stop", 0, 32766)
#define INERTIA_BOOL_FIELDS(F, n) F(n, cancel_scroll_inertia_on_ctrl, "cancel_on_ctrl")
struct inertia_params {
#define INT_MEMBER(n, field, key, lo, hi) int32_t field;
#define BOOL_MEMBER(n, field, key) bool field;
    INERTIA_INT_FIELDS(INT_MEMBER, 0)
    INERTIA_BOOL_FIELDS(BOOL_MEMBER, 0)
#undef INT_MEMBER
#undef BOOL_MEMBER
};
int inertia_get_params(const struct device *dev, struct inertia_params *out);
/* Invalid sets are atomic; equal sets are no-ops. Changed sets cancel all
 * streams/generations without reusing queued token slots. An already committed
 * HID transaction may finish; the setter never waits on endpoint submission. */
int inertia_set_params(const struct device *dev, const struct inertia_params *params);
