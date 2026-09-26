/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <zmk-input-inertia/inertia_core.h>

typedef long atomic_t;
typedef long atomic_val_t;
typedef struct { int64_t ms; } k_timeout_t;
struct k_mutex { int depth; };
struct k_work { void (*handler)(struct k_work *work); };
struct k_work_delayable { struct k_work work; bool scheduled; int64_t delay_ms; };
struct k_work_q { int unused; };
struct device { const void *config; void *data; const char *name; };
struct input_event { uint8_t sync; uint16_t type; uint16_t code; int32_t value; };
struct zmk_input_processor_state { uint8_t input_device_index; };
struct keyboard_report { struct { uint8_t modifiers; } body; };
struct inertia_test_snapshot {
    int16_t velocity[INERTIA_AXIS_COUNT];
    int16_t ema[INERTIA_AXIS_COUNT];
    uint32_t generation;
    uint32_t emit_count;
    bool active;
    bool inertial;
    bool frame_open;
};

#define IS_ENABLED(option) option
#define CONFIG_ZMK_INPUT_INERTIA_TEST 1
#define ARG_UNUSED(x) ((void)(x))
#define CONTAINER_OF(ptr, type, field) ((type *)(void *)((char *)(ptr) - offsetof(type, field)))
#define K_FOREVER ((k_timeout_t){-1})
#define K_MSEC(ms) ((k_timeout_t){(ms)})
#define INPUT_EV_KEY 0x01
#define INPUT_EV_REL 0x02
#define INPUT_REL_X 0x00
#define INPUT_REL_Y 0x01
#define INPUT_REL_HWHEEL 0x06
#define INPUT_REL_WHEEL 0x08
#define INPUT_REL_MISC 0x09
#define ZMK_INPUT_PROC_CONTINUE 0
#define MOD_LCTL 0x01
#define MOD_RCTL 0x10
#define INERTIA_STREAM_COUNT 2

struct inertia_config {
    uint16_t move_decay_factor_q8;
    uint16_t move_interval_ms;
    uint16_t move_threshold_start;
    uint16_t move_threshold_stop;
    uint16_t scroll_decay_factor_q8;
    uint16_t scroll_interval_ms;
    uint16_t scroll_threshold_start;
    uint16_t scroll_threshold_stop;
    uint16_t trigger_ms;
    bool cancel_scroll_inertia_on_ctrl;
};
struct inertia_motion_state {
    int16_t velocity[INERTIA_AXIS_COUNT];
    int16_t ema[INERTIA_AXIS_COUNT];
    int16_t remainder_q8[INERTIA_AXIS_COUNT];
    struct inertia_frame frame;
    atomic_t generation;
    bool active;
    bool inertial;
};
struct inertia_data;
struct inertia_stream {
    struct inertia_data *owner;
    struct inertia_motion_state move;
    struct inertia_motion_state scroll;
    struct k_work_delayable move_work;
    struct k_work_delayable scroll_work;
    atomic_t move_emit_count;
    atomic_t scroll_emit_count;
};
struct inertia_data {
    const struct device *dev;
    struct inertia_stream streams[INERTIA_STREAM_COUNT];
    struct k_mutex lock;
};

static struct keyboard_report keyboard;
static struct k_work_q lowprio_queue;
static int16_t hid_move[2], hid_scroll[2];
static int sent_reports;
static int16_t sent_move[2], sent_scroll[2];
/* Which glide the emit hook should be told about. */
static bool expect_scroll;
/* Runs in the gap between deciding to emit and emitting. */
static void (*before_emit)(struct inertia_stream *stream);
static struct inertia_data data;

static atomic_val_t atomic_get(const atomic_t *value) { return *value; }
static void atomic_set(atomic_t *value, atomic_val_t next) { *value = next; }
static void atomic_inc(atomic_t *value) { (*value)++; }
static void k_mutex_init(struct k_mutex *mutex) { mutex->depth = 0; }
static int locks;
static int k_mutex_lock(struct k_mutex *mutex, k_timeout_t timeout) {
    ARG_UNUSED(timeout);
    locks++;
    assert(mutex->depth == 0); /* never taken recursively */
    mutex->depth++;
    return 0;
}
static int k_mutex_unlock(struct k_mutex *mutex) {
    assert(mutex->depth == 1);
    mutex->depth--;
    return 0;
}
static struct k_work_q *zmk_workqueue_lowprio_work_q(void) { return &lowprio_queue; }
static void k_work_init_delayable(struct k_work_delayable *work,
                                  void (*handler)(struct k_work *work)) {
    *work = (struct k_work_delayable){.work = {.handler = handler}};
}
static struct k_work_delayable *k_work_delayable_from_work(struct k_work *work) {
    return CONTAINER_OF(work, struct k_work_delayable, work);
}
static int k_work_reschedule_for_queue(struct k_work_q *queue, struct k_work_delayable *work,
                                       k_timeout_t delay) {
    assert(queue == &lowprio_queue);
    work->scheduled = true;
    work->delay_ms = delay.ms;
    return 1;
}
static int k_work_cancel_delayable(struct k_work_delayable *work) {
    work->scheduled = false;
    return 0;
}
static struct keyboard_report *zmk_hid_get_keyboard_report(void) { return &keyboard; }
static void zmk_hid_mouse_movement_set(int16_t x, int16_t y) {
    hid_move[0] = x;
    hid_move[1] = y;
}
static void zmk_hid_mouse_scroll_set(int16_t x, int16_t y) {
    hid_scroll[0] = x;
    hid_scroll[1] = y;
}
static int zmk_endpoint_send_mouse_report(void) {
    sent_reports++;
    sent_move[0] = hid_move[0];
    sent_move[1] = hid_move[1];
    sent_scroll[0] = hid_scroll[0];
    sent_scroll[1] = hid_scroll[1];
    return 0;
}
static void inertia_test_before_emit(const struct device *dev, size_t stream_index, bool scroll) {
    assert(dev == data.dev && stream_index < INERTIA_STREAM_COUNT && data.lock.depth == 0);
    assert(scroll == expect_scroll);
    if (before_emit != NULL) {
        void (*hook)(struct inertia_stream *stream) = before_emit;
        before_emit = NULL;
        hook(&data.streams[stream_index]);
    }
}

/* DRIVER_FUNCTIONS */

static struct device processor;
static struct inertia_config config;
static struct zmk_input_processor_state second = {.input_device_index = 1};

static struct inertia_stream *stream(size_t index) { return &data.streams[index]; }

static void send_to(struct zmk_input_processor_state *state, uint16_t type, uint16_t code,
                    int32_t value, bool sync) {
    struct input_event event = {.sync = sync, .type = type, .code = code, .value = value};
    assert(inertia_handle_event(&processor, &event, 0, 0, state) == ZMK_INPUT_PROC_CONTINUE);
    assert(event.code == code && event.value == value); /* never altered */
}

static void move(struct zmk_input_processor_state *state, int32_t x, int32_t y) {
    send_to(state, INPUT_EV_REL, INPUT_REL_X, x, false);
    send_to(state, INPUT_EV_REL, INPUT_REL_Y, y, true);
}

static void wheel(struct zmk_input_processor_state *state, int32_t x, int32_t y) {
    send_to(state, INPUT_EV_REL, INPUT_REL_HWHEEL, x, false);
    send_to(state, INPUT_EV_REL, INPUT_REL_WHEEL, y, true);
}

static void run_move(size_t index) {
    stream(index)->move_work.scheduled = false;
    stream(index)->move_work.work.handler(&stream(index)->move_work.work);
}

static void run_scroll(size_t index) {
    stream(index)->scroll_work.scheduled = false;
    stream(index)->scroll_work.work.handler(&stream(index)->scroll_work.work);
}

static void bump_move_generation(struct inertia_stream *s) { atomic_inc(&s->move.generation); }
static void bump_scroll_generation(struct inertia_stream *s) {
    atomic_inc(&s->scroll.generation);
}
static void stop_move(struct inertia_stream *s) { s->move.active = false; }
static void settle_move(struct inertia_stream *s) { s->move.inertial = false; }
static void stop_scroll(struct inertia_stream *s) { s->scroll.active = false; }
static void settle_scroll(struct inertia_stream *s) { s->scroll.inertial = false; }

static void test_routing_and_frames(void) {
    struct zmk_input_processor_state stranger = {.input_device_index = INERTIA_STREAM_COUNT};
    data.lock.depth = 3;
    for (size_t i = 0; i < INERTIA_STREAM_COUNT; i++) {
        struct inertia_motion_state *motions[] = {&stream(i)->move, &stream(i)->scroll};
        for (size_t m = 0; m < 2; m++) {
            motions[m]->velocity[INERTIA_AXIS_X] = motions[m]->velocity[INERTIA_AXIS_Y] = 5;
            motions[m]->ema[INERTIA_AXIS_X] = motions[m]->ema[INERTIA_AXIS_Y] = 5;
            motions[m]->remainder_q8[INERTIA_AXIS_X] = motions[m]->remainder_q8[INERTIA_AXIS_Y] = 5;
            motions[m]->frame.open = motions[m]->active = motions[m]->inertial = true;
            motions[m]->generation = 9;
        }
        stream(i)->move_emit_count = stream(i)->scroll_emit_count = 9;
    }
    assert(inertia_init(&processor) == 0);
    assert(data.dev == &processor && stream(1)->owner == &data && data.lock.depth == 0);
    for (size_t i = 0; i < INERTIA_STREAM_COUNT; i++) {
        struct inertia_motion_state *motions[] = {&stream(i)->move, &stream(i)->scroll};
        for (size_t m = 0; m < 2; m++) {
            assert(!motion_has_state(motions[m]) && !motions[m]->inertial);
            assert(motions[m]->generation == 0);
        }
        assert(stream(i)->move_emit_count == 0 && stream(i)->scroll_emit_count == 0);
    }

    /* Past the listeners, or neither motion nor a sync: untouched. */
    send_to(&stranger, INPUT_EV_REL, INPUT_REL_X, 50, true);
    send_to(NULL, INPUT_EV_KEY, INPUT_REL_X, 1, false);
    const int held = locks;
    send_to(NULL, INPUT_EV_REL, INPUT_REL_MISC, 1, false);
    /* A key whose code happens to be a wheel's is not a wheel. */
    send_to(NULL, INPUT_EV_KEY, INPUT_REL_WHEEL, 1, false);
    send_to(NULL, INPUT_EV_KEY, INPUT_REL_HWHEEL, 1, false);
    send_to(NULL, INPUT_EV_KEY, INPUT_REL_X, 1, false);
    send_to(NULL, INPUT_EV_KEY, INPUT_REL_Y, 1, false);
    assert(locks == held); /* passed through without taking the lock */
    assert(!stream(0)->move.frame.open && !stream(0)->scroll.frame.open);
    /* A bare sync closes nothing that is not open. */
    send_to(NULL, INPUT_EV_KEY, 0, 1, true);

    /* Below the start threshold on both axes: nothing carries on. */
    move(NULL, 3, -3);
    assert(!stream(0)->move.active && !stream(0)->move_work.scheduled);
    /* Exactly at the start threshold counts, on either axis. */
    move(NULL, 15, 0);
    assert(stream(0)->move.active);
    move(NULL, 0, 15);
    assert(stream(0)->move.active);
    move(NULL, 14, 14);
    assert(!stream(0)->move.active);
    /* Frames average into the EMA until a glide starts. */
    move(NULL, 40, 0);
    assert(stream(0)->move.ema[INERTIA_AXIS_X] == 20);
    move(NULL, 40, 0);
    assert(stream(0)->move.ema[INERTIA_AXIS_X] == 30);
    wheel(NULL, 0, 8);
    assert(stream(0)->scroll.ema[INERTIA_AXIS_Y] == 4);
    wheel(NULL, 0, 8);
    assert(stream(0)->scroll.ema[INERTIA_AXIS_Y] == 6);
    /* X alone past it, then Y alone: armed after trigger-ms. */
    move(NULL, 20, 0);
    assert(stream(0)->move.active && stream(0)->move_work.delay_ms == 30);
    move(NULL, 0, -20);
    assert(stream(0)->move.active && stream(0)->move.velocity[INERTIA_AXIS_Y] == -20);
}

static void test_move_decay(void) {
    /* The glide emits, reschedules and slows until it stops. */
    move(NULL, 40, 40);
    run_move(0);
    assert(sent_reports == 1 && sent_move[0] > 0 && sent_move[1] > 0);
    assert(hid_move[0] == 0 && hid_move[1] == 0); /* the report is left clean */
    assert(stream(0)->move.inertial && stream(0)->move_work.delay_ms == 10);
    int guard = 0;
    while (stream(0)->move.active && guard++ < 100) {
        run_move(0);
    }
    assert(!stream(0)->move.active && !stream(0)->move.inertial);
    assert(!stream(0)->move_work.scheduled); /* a stopped glide is not rescheduled */
    const int emitted = sent_reports;
    run_move(0); /* already stopped */
    assert(sent_reports == emitted);

    /* One axis still above the stop threshold keeps it going. */
    move(NULL, 0, 40);
    run_move(0);
    assert(sent_reports == emitted + 1 && sent_move[0] == 0);

    /* New movement over an inertial glide starts from a clean state. */
    move(NULL, 30, 0);
    assert(!stream(0)->move.inertial && stream(0)->move.ema[INERTIA_AXIS_Y] == 0);

    /* Each thing that can change in the emit gap cancels the emit. */
    void (*gaps[])(struct inertia_stream *) = {bump_move_generation, stop_move, settle_move};
    for (size_t i = 0; i < sizeof(gaps) / sizeof(gaps[0]); i++) {
        move(NULL, 40, 40);
        const int before = sent_reports;
        before_emit = gaps[i];
        run_move(0);
        assert(sent_reports == before);
    }
    move(NULL, 1, 1);
}

static void test_scroll_decay(void) {
    expect_scroll = true;
    wheel(NULL, 0, 6);
    assert(stream(0)->scroll.active);
    run_scroll(0);
    assert(sent_scroll[1] > 0 && hid_scroll[0] == 0 && hid_scroll[1] == 0);
    assert(stream(0)->scroll_emit_count == 1);
    int guard = 0;
    while (stream(0)->scroll.active && guard++ < 100) {
        run_scroll(0);
    }
    assert(!stream(0)->scroll.active && !stream(0)->scroll_work.scheduled);
    run_scroll(0);

    void (*gaps[])(struct inertia_stream *) = {bump_scroll_generation, stop_scroll,
                                               settle_scroll};
    for (size_t i = 0; i < sizeof(gaps) / sizeof(gaps[0]); i++) {
        wheel(NULL, 6, 0);
        const int before = sent_reports;
        before_emit = gaps[i];
        run_scroll(0);
        assert(sent_reports == before);
    }

    /* Ctrl held: the worker drops the glide, new wheel input cancels every
     * glide and is passed on, and without the option Ctrl changes nothing. */
    config.cancel_scroll_inertia_on_ctrl = true;
    wheel(NULL, 0, 6);
    run_scroll(0);
    keyboard.body.modifiers = MOD_RCTL;
    const atomic_val_t generation = stream(0)->scroll.generation;
    run_scroll(0);
    assert(!stream(0)->scroll.active && stream(0)->scroll.generation == generation + 1);

    /* A move and a scroll cancel each other, so each is tried on its own. */
    keyboard.body.modifiers = 0;
    move(NULL, 40, 0);
    keyboard.body.modifiers = MOD_LCTL;
    send_to(NULL, INPUT_EV_REL, INPUT_REL_WHEEL, 3, true);
    assert(!stream(0)->move.active && !stream(0)->scroll.frame.open);
    keyboard.body.modifiers = 0;
    wheel(&second, 0, 6);
    keyboard.body.modifiers = MOD_LCTL;
    send_to(NULL, INPUT_EV_REL, INPUT_REL_WHEEL, 3, true);
    assert(!stream(1)->scroll.active && !stream(0)->scroll.frame.open);

    config.cancel_scroll_inertia_on_ctrl = false;
    wheel(NULL, 0, 6);
    assert(stream(0)->scroll.active);
    run_scroll(0);
    assert(stream(0)->scroll.inertial);
    keyboard.body.modifiers = 0;
}

static void test_cross_stream(void) {
    /* Movement on one stream stops scrolling everywhere and other streams'
     * movement; scrolling does the reverse. */
    wheel(NULL, 0, 6);
    move(&second, 40, 0);
    assert(!stream(0)->scroll.active && stream(1)->move.active);
    move(NULL, 40, 0);
    assert(!stream(1)->move.active && stream(0)->move.active);
    wheel(&second, 0, 6);
    assert(!stream(0)->move.active && stream(1)->scroll.active);
    wheel(NULL, 6, 0);
    assert(!stream(1)->scroll.active && stream(0)->scroll.active);
    /* An inertial scroll on the same stream is restarted cleanly. */
    run_scroll(0);
    assert(stream(0)->scroll.inertial);
    wheel(NULL, 0, 6);
    assert(!stream(0)->scroll.inertial);
}

static void test_snapshot(void) {
    expect_scroll = false;
    struct inertia_test_snapshot snapshot;
    assert(inertia_test_get_snapshot(NULL, 0, false, &snapshot) == -EINVAL);
    assert(inertia_test_get_snapshot(&processor, 0, false, NULL) == -EINVAL);
    assert(inertia_test_get_snapshot(&processor, INERTIA_STREAM_COUNT, false, &snapshot) ==
           -EINVAL);
    move(NULL, 40, 30);
    run_move(0);
    assert(inertia_test_get_snapshot(&processor, 0, false, &snapshot) == 0);
    assert(snapshot.active && snapshot.inertial && !snapshot.frame_open);
    assert(snapshot.emit_count > 0 && snapshot.velocity[INERTIA_AXIS_X] > 0);
    const struct inertia_motion_state *m = &stream(0)->move;
    assert(snapshot.velocity[INERTIA_AXIS_X] == sent_move[0] &&
           snapshot.velocity[INERTIA_AXIS_Y] == sent_move[1] && sent_move[1] != 0);
    assert(snapshot.ema[INERTIA_AXIS_X] == m->ema[INERTIA_AXIS_X] &&
           snapshot.ema[INERTIA_AXIS_Y] == m->ema[INERTIA_AXIS_Y] && m->ema[INERTIA_AXIS_Y] != 0);
    assert(snapshot.generation == (uint32_t)m->generation && m->generation != 0);
    assert(snapshot.emit_count == (uint32_t)stream(0)->move_emit_count);
    send_to(NULL, INPUT_EV_REL, INPUT_REL_X, 3, false); /* a frame left open */
    assert(inertia_test_get_snapshot(&processor, 0, false, &snapshot) == 0);
    assert(snapshot.frame_open);
    send_to(NULL, INPUT_EV_REL, INPUT_REL_Y, 0, true);
    assert(inertia_test_get_snapshot(&processor, 0, true, &snapshot) == 0);
    assert(!snapshot.active);
}

int main(void) {
    config = (struct inertia_config){
        .move_decay_factor_q8 = 230, .move_interval_ms = 10, .move_threshold_start = 15,
        .move_threshold_stop = 2, .scroll_decay_factor_q8 = 200, .scroll_interval_ms = 20,
        .scroll_threshold_start = 2, .scroll_threshold_stop = 0, .trigger_ms = 30};
    processor = (struct device){.config = &config, .data = &data, .name = "inertia"};

    test_routing_and_frames();
    test_move_decay();
    test_scroll_decay();
    test_cross_stream();
    test_snapshot();
    assert(data.lock.depth == 0);
    puts("inertia driver: PASS");
    return 0;
}
