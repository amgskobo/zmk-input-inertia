/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <zmk-input-inertia/inertia_core.h>

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

static int16_t reference_saturate_i16(int64_t value) {
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)value;
}

static void reference_decay_axis(int16_t input, uint16_t factor_q8, int16_t *output,
                                 int16_t *remainder_q8) {
    int64_t ideal_q8 = (int64_t)input * INERTIA_Q8_SCALE + *remainder_q8;
    int64_t decayed_q8 = ideal_q8 * factor_q8 / INERTIA_Q8_SCALE;
    int64_t rounded = decayed_q8 >= 0 ? (decayed_q8 + INERTIA_Q8_HALF) / INERTIA_Q8_SCALE
                                      : -((-decayed_q8 + INERTIA_Q8_HALF) / INERTIA_Q8_SCALE);

    *output = reference_saturate_i16(rounded);
    *remainder_q8 = (int16_t)(decayed_q8 - (int64_t)*output * INERTIA_Q8_SCALE);
}

static void test_half(void) {
    assert(inertia_half(0) == 0);
    assert(inertia_half(1) == 1);
    assert(inertia_half(-1) == -1);
    assert(inertia_half(2) == 1);
    assert(inertia_half(-2) == -1);
    assert(inertia_half(3) == 2);
    assert(inertia_half(-3) == -2);
    assert(inertia_half(65534) == INT16_MAX);
    assert(inertia_half(-65536) == INT16_MIN);
    assert(inertia_half(INT32_MAX) == INT16_MAX);
    assert(inertia_half(INT32_MIN) == INT16_MIN);
}

static void test_saturation(void) {
    assert(inertia_saturate_i16(INT32_MIN) == INT16_MIN);
    assert(inertia_saturate_i16(INT16_MIN - 1) == INT16_MIN);
    assert(inertia_saturate_i16(INT16_MIN) == INT16_MIN);
    assert(inertia_saturate_i16(0) == 0);
    assert(inertia_saturate_i16(INT16_MAX) == INT16_MAX);
    assert(inertia_saturate_i16(INT16_MAX + 1) == INT16_MAX);
    assert(inertia_saturate_i16(INT32_MAX) == INT16_MAX);
}

static void test_decay_equivalence(void) {
    static const uint16_t factors[] = {0U, 2U, 128U, 230U, 253U};
    static const int16_t remainders[] = {
        INT16_MIN, -257, -128, -127, -1, 0, 1, 127, 128, 257, INT16_MAX,
    };

    for (int32_t input = INT16_MIN; input <= INT16_MAX; input++) {
        for (size_t factor_index = 0U; factor_index < ARRAY_SIZE(factors); factor_index++) {
            for (size_t remainder_index = 0U; remainder_index < ARRAY_SIZE(remainders);
                 remainder_index++) {
                int16_t expected_remainder = remainders[remainder_index];
                int16_t actual_remainder = expected_remainder;
                int16_t expected;
                int16_t actual;

                reference_decay_axis((int16_t)input, factors[factor_index], &expected,
                                     &expected_remainder);
                inertia_decay_axis((int16_t)input, factors[factor_index], &actual,
                                   &actual_remainder);

                assert(actual == expected);
                assert(actual_remainder == expected_remainder);
            }
        }
    }
}

static void test_decay_factor_is_bounded(void) {
    int16_t bounded_remainder = 0;
    int16_t excessive_remainder = 0;
    int16_t bounded;
    int16_t excessive;

    inertia_decay_axis(INT16_MAX, INERTIA_Q8_MAX_FACTOR, &bounded, &bounded_remainder);
    inertia_decay_axis(INT16_MAX, UINT16_MAX, &excessive, &excessive_remainder);

    assert(excessive == bounded);
    assert(excessive_remainder == bounded_remainder);
}

static void test_decay_symmetry(void) {
    for (int32_t input = 0; input <= INT16_MAX; input += 31) {
        int16_t positive_remainder = 0;
        int16_t negative_remainder = 0;
        int16_t positive;
        int16_t negative;

        inertia_decay_axis((int16_t)input, 230U, &positive, &positive_remainder);
        inertia_decay_axis((int16_t)-input, 230U, &negative, &negative_remainder);

        assert(negative == -positive);
        assert(negative_remainder == -positive_remainder);
    }
}

static void test_decay_converges(void) {
    int16_t positive = INT16_MAX;
    int16_t negative = INT16_MIN;
    int16_t positive_remainder = 0;
    int16_t negative_remainder = 0;

    for (size_t iteration = 0U; iteration < 5000U; iteration++) {
        inertia_decay_axis(positive, 253U, &positive, &positive_remainder);
        inertia_decay_axis(negative, 253U, &negative, &negative_remainder);
    }

    assert(positive == 0);
    assert(negative == 0);
}

static void test_frames(void) {
    struct inertia_frame frame;
    int16_t output[INERTIA_AXIS_COUNT];

    inertia_frame_init(&frame);
    assert(!inertia_frame_finish(&frame, output));

    inertia_frame_add(&frame, INERTIA_AXIS_COUNT, 123);
    assert(!inertia_frame_finish(&frame, output));

    inertia_frame_add(&frame, INERTIA_AXIS_X, 10);
    assert(inertia_frame_finish(&frame, output));
    assert(output[INERTIA_AXIS_X] == 10);
    assert(output[INERTIA_AXIS_Y] == 0);

    inertia_frame_add(&frame, INERTIA_AXIS_Y, -20);
    assert(inertia_frame_finish(&frame, output));
    assert(output[INERTIA_AXIS_X] == 0);
    assert(output[INERTIA_AXIS_Y] == -20);

    inertia_frame_add(&frame, INERTIA_AXIS_X, 10000);
    inertia_frame_add(&frame, INERTIA_AXIS_X, 30000);
    inertia_frame_add(&frame, INERTIA_AXIS_Y, -10000);
    inertia_frame_add(&frame, INERTIA_AXIS_Y, -30000);
    assert(inertia_frame_finish(&frame, output));
    assert(output[INERTIA_AXIS_X] == INT16_MAX);
    assert(output[INERTIA_AXIS_Y] == INT16_MIN);

    inertia_frame_add(&frame, INERTIA_AXIS_X, INT32_MAX);
    inertia_frame_add(&frame, INERTIA_AXIS_X, 1);
    inertia_frame_add(&frame, INERTIA_AXIS_Y, INT32_MIN);
    inertia_frame_add(&frame, INERTIA_AXIS_Y, -1);
    assert(inertia_frame_finish(&frame, output));
    assert(output[INERTIA_AXIS_X] == INT16_MAX);
    assert(output[INERTIA_AXIS_Y] == INT16_MIN);
}

int main(void) {
    test_half();
    test_saturation();
    test_decay_equivalence();
    test_decay_factor_is_bounded();
    test_decay_symmetry();
    test_decay_converges();
    test_frames();

    puts("inertia core tests: PASS");
    return 0;
}
