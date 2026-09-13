/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define INERTIA_Q8_SCALE 256
#define INERTIA_Q8_HALF 128
#define INERTIA_Q8_MAX_FACTOR 253

enum inertia_axis {
    INERTIA_AXIS_X = 0,
    INERTIA_AXIS_Y = 1,
    INERTIA_AXIS_COUNT = 2,
};

struct inertia_frame {
    int32_t delta[INERTIA_AXIS_COUNT];
    bool open;
};

int16_t inertia_half(int32_t sum);
int16_t inertia_saturate_i16(int32_t value);
void inertia_decay_axis(int16_t input, uint16_t factor_q8, int16_t *output, int16_t *remainder_q8);

void inertia_frame_init(struct inertia_frame *frame);
void inertia_frame_add(struct inertia_frame *frame, enum inertia_axis axis, int32_t value);
bool inertia_frame_finish(struct inertia_frame *frame, int16_t output[INERTIA_AXIS_COUNT]);
