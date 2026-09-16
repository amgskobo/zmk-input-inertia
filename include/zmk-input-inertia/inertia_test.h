/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>

#include <zmk-input-inertia/inertia_core.h>

struct inertia_test_snapshot {
    int16_t velocity[INERTIA_AXIS_COUNT];
    int16_t ema[INERTIA_AXIS_COUNT];
    uint32_t generation;
    uint32_t emit_count;
    bool active;
    bool inertial;
    bool frame_open;
};

int inertia_test_get_snapshot(const struct device *dev, size_t stream_index, bool scroll,
                              struct inertia_test_snapshot *snapshot);
void inertia_test_before_emit(const struct device *dev, size_t stream_index, bool scroll);
