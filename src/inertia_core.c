/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#include <limits.h>
#include <stddef.h>

#include <zmk-input-inertia/inertia_core.h>

static int32_t saturating_add_i32(int32_t left, int32_t right) {
    if (right > 0 && left > INT32_MAX - right) {
        return INT32_MAX;
    }
    if (right < 0 && left < INT32_MIN - right) {
        return INT32_MIN;
    }

    return left + right;
}

int16_t inertia_half(int32_t sum) {
    if (sum >= (int32_t)INT16_MAX * 2) {
        return INT16_MAX;
    }
    if (sum <= (int32_t)INT16_MIN * 2) {
        return INT16_MIN;
    }

    return (int16_t)(sum >= 0 ? (sum + 1) / 2 : -((-sum + 1) / 2));
}

int16_t inertia_saturate_i16(int32_t value) {
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }

    return (int16_t)value;
}

void inertia_decay_axis(int16_t input, uint16_t factor_q8, int16_t *output, int16_t *remainder_q8) {
    if (factor_q8 > INERTIA_Q8_MAX_FACTOR) {
        factor_q8 = INERTIA_Q8_MAX_FACTOR;
    }

    int32_t ideal_q8 = (int32_t)input * INERTIA_Q8_SCALE + *remainder_q8;
    int32_t decayed_q8 = ideal_q8 * (int32_t)factor_q8 / INERTIA_Q8_SCALE;
    int32_t rounded;

    if (decayed_q8 >= 0) {
        rounded = (decayed_q8 + INERTIA_Q8_HALF) / INERTIA_Q8_SCALE;
    } else {
        rounded = -((-decayed_q8 + INERTIA_Q8_HALF) / INERTIA_Q8_SCALE);
    }

    *output = inertia_saturate_i16(rounded);
    *remainder_q8 = (int16_t)(decayed_q8 - (int32_t)*output * INERTIA_Q8_SCALE);
}

void inertia_frame_init(struct inertia_frame *frame) {
    frame->delta[INERTIA_AXIS_X] = 0;
    frame->delta[INERTIA_AXIS_Y] = 0;
    frame->open = false;
}

void inertia_frame_add(struct inertia_frame *frame, enum inertia_axis axis, int32_t value) {
    if (axis < INERTIA_AXIS_X || axis >= INERTIA_AXIS_COUNT) {
        return;
    }

    if (!frame->open) {
        inertia_frame_init(frame);
        frame->open = true;
    }

    frame->delta[axis] = saturating_add_i32(frame->delta[axis], value);
}

bool inertia_frame_finish(struct inertia_frame *frame, int16_t output[INERTIA_AXIS_COUNT]) {
    if (!frame->open) {
        return false;
    }

    output[INERTIA_AXIS_X] = inertia_saturate_i16(frame->delta[INERTIA_AXIS_X]);
    output[INERTIA_AXIS_Y] = inertia_saturate_i16(frame->delta[INERTIA_AXIS_Y]);
    inertia_frame_init(frame);
    return true;
}
