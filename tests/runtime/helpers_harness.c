/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <zmk-input-inertia/inertia_core.h>

typedef int atomic_t;
struct k_work_delayable { int unused; };
struct k_mutex { int unused; };
struct device { const void *config; void *data; };
struct inertia_motion_state {
    int16_t velocity[INERTIA_AXIS_COUNT];
    int16_t ema[INERTIA_AXIS_COUNT];
    int16_t remainder_q8[INERTIA_AXIS_COUNT];
    struct inertia_frame frame;
    atomic_t generation;
    bool active, inertial;
};
struct inertia_data;
struct inertia_stream {
    struct inertia_data *owner;
    struct inertia_motion_state move, scroll;
    struct k_work_delayable move_work, scroll_work;
};
#define INERTIA_STREAM_COUNT 2
struct inertia_data {
    const struct device *dev;
    struct inertia_stream streams[INERTIA_STREAM_COUNT];
    struct k_mutex lock;
};
struct zmk_input_processor_state { size_t input_device_index; };
struct keyboard_report { struct { unsigned modifiers; } body; };
static struct keyboard_report report;
#define MOD_LCTL 1u
#define MOD_RCTL 2u
static struct keyboard_report *zmk_hid_get_keyboard_report(void) { return &report; }
static int cancellations;
static struct k_work_delayable *cancelled[8];
static bool k_work_cancel_delayable(struct k_work_delayable *work) {
    cancelled[cancellations++ % 8] = work;
    return true;
}
static bool was_cancelled(int since, const struct k_work_delayable *work) {
    for (int i = since; i < cancellations; i++) {
        if (cancelled[i % 8] == work) {
            return true;
        }
    }
    return false;
}

/* Every field a motion can hold, set. */
static void fill(struct inertia_motion_state *motion) {
    for (int axis = 0; axis < INERTIA_AXIS_COUNT; ++axis) {
        motion->velocity[axis] = 3;
        motion->ema[axis] = 4;
        motion->remainder_q8[axis] = 5;
    }
    motion->active = motion->inertial = true;
    motion->frame.open = true;
}
static void atomic_inc(atomic_t *value) { ++*value; }

/* DRIVER_FUNCTIONS */

static void check_motion_flags(void) {
    struct inertia_motion_state motion = {0};
    assert(!motion_has_state(&motion));
    motion.active = true;
    assert(motion_has_state(&motion));
    motion.active = false;
    motion.frame.open = true;
    assert(motion_has_state(&motion));
    motion.frame.open = false;
    for (int axis = 0; axis < INERTIA_AXIS_COUNT; ++axis) {
        motion.velocity[axis] = 1;
        assert(motion_has_state(&motion));
        motion.velocity[axis] = 0;
    }
    for (int axis = 0; axis < INERTIA_AXIS_COUNT; ++axis) {
        motion.ema[axis] = 1;
        assert(motion_has_state(&motion));
        motion.ema[axis] = 0;
    }
    for (int axis = 0; axis < INERTIA_AXIS_COUNT; ++axis) {
        motion.remainder_q8[axis] = 1;
        assert(motion_has_state(&motion));
        motion.remainder_q8[axis] = 0;
    }
    assert(!motion_has_state(&motion));
}

int main(void) {
    assert(inertia_abs_i16(123) == 123);
    assert(inertia_abs_i16(INT16_MIN) == 32768);
    report.body.modifiers = 0;
    assert(!ctrl_mod_is_active());
    report.body.modifiers = MOD_LCTL;
    assert(ctrl_mod_is_active());
    report.body.modifiers = MOD_RCTL;
    assert(ctrl_mod_is_active());
    check_motion_flags();

    struct inertia_data data = {0};
    struct inertia_stream *first = &data.streams[0], *second = &data.streams[1];
    struct zmk_input_processor_state state = {0};
    assert(stream_for_event(&data, NULL) == first);
    assert(stream_for_event(&data, &state) == first);
    state.input_device_index = 1;
    assert(stream_for_event(&data, &state) == second);
    state.input_device_index = 2;
    assert(stream_for_event(&data, &state) == NULL);

    fill(&first->move);
    int since = cancellations;
    cancel_move_locked(first);
    assert(first->move.generation == 1 && !motion_has_state(&first->move));
    assert(!first->move.inertial && was_cancelled(since, &first->move_work));
    fill(&first->scroll);
    since = cancellations;
    cancel_scroll_locked(first);
    assert(first->scroll.generation == 1 && !motion_has_state(&first->scroll));
    assert(!first->scroll.inertial && was_cancelled(since, &first->scroll_work));

    first->move.frame.open = true;
    int before = cancellations;
    begin_move_frame_locked(&data, first);
    assert(cancellations == before);
    first->move.frame.open = false;
    first->scroll.active = true;
    second->move.active = true;
    first->move.inertial = true;
    begin_move_frame_locked(&data, first);
    assert(!first->scroll.active && !second->move.active && !first->move.inertial);
    /* The target's own running average survives a new frame. */
    fill(&second->move);
    second->move.frame.open = false;
    second->move.inertial = false;
    second->move.ema[0] = 9;
    const int move_generation = second->move.generation;
    since = cancellations;
    begin_move_frame_locked(&data, second);
    assert(!second->move.active && !second->move.inertial);
    assert(was_cancelled(since, &second->move_work));
    for (int axis = 0; axis < INERTIA_AXIS_COUNT; ++axis) {
        assert(second->move.velocity[axis] == 0 && second->move.remainder_q8[axis] == 0);
    }
    assert(second->move.ema[0] == 9 && second->move.generation == move_generation + 1);

    first->scroll.frame.open = true;
    before = cancellations;
    begin_scroll_frame_locked(&data, first);
    assert(cancellations == before);
    first->scroll.frame.open = false;
    first->move.active = true;
    second->scroll.active = true;
    first->scroll.inertial = true;
    begin_scroll_frame_locked(&data, first);
    assert(!first->move.active && !second->scroll.active && !first->scroll.inertial);
    /* The target's own running average survives a new frame. */
    fill(&second->scroll);
    second->scroll.frame.open = false;
    second->scroll.inertial = false;
    second->scroll.ema[0] = 9;
    const int scroll_generation = second->scroll.generation;
    since = cancellations;
    begin_scroll_frame_locked(&data, second);
    assert(!second->scroll.active && !second->scroll.inertial);
    assert(was_cancelled(since, &second->scroll_work));
    for (int axis = 0; axis < INERTIA_AXIS_COUNT; ++axis) {
        assert(second->scroll.velocity[axis] == 0 && second->scroll.remainder_q8[axis] == 0);
    }
    assert(second->scroll.ema[0] == 9 && second->scroll.generation == scroll_generation + 1);
    puts("inertia stream/frame helpers: PASS");
    return 0;
}
