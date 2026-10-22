/*
 * Copyright (c) 2025-2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_inertia

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <drivers/input_processor.h>
#include <dt-bindings/zmk/modifiers.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>
#include <zmk/workqueue.h>

#include <zmk-input-inertia/inertia_core.h>
#include <zmk-input-inertia/runtime.h>
#if IS_ENABLED(CONFIG_ZMK_INPUT_INERTIA_TEST)
#include <zmk-input-inertia/inertia_test.h>
#endif

#ifndef CONFIG_ZMK_INPUT_INERTIA_LOG_LEVEL
#define CONFIG_ZMK_INPUT_INERTIA_LOG_LEVEL 3
#endif
LOG_MODULE_REGISTER(zmk_input_inertia_input_processor_inertia, CONFIG_ZMK_INPUT_INERTIA_LOG_LEVEL);

#define INERTIA_LISTENER_COUNT DT_NUM_INST_STATUS_OKAY(zmk_input_listener)
#define INERTIA_STREAM_COUNT MAX(INERTIA_LISTENER_COUNT, 1)
#define INERTIA_FACTOR_Q8(percent) ((percent) * INERTIA_Q8_SCALE / 100)

/* Normal ZMK pointing listeners and inertia emission must share one consumer.
 * Synchronous input callbacks would reintroduce cross-thread HID mutation. */
BUILD_ASSERT(IS_ENABLED(CONFIG_INPUT_MODE_THREAD),
             "Inertia requires CONFIG_INPUT_MODE_THREAD");

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

struct inertia_pending_report {
    uint32_t generation;
    int16_t output[INERTIA_AXIS_COUNT];
    bool queued;
};

struct inertia_data;

struct inertia_stream {
    struct inertia_data *owner;
    struct inertia_motion_state move;
    struct inertia_motion_state scroll;
    struct k_work_delayable move_work;
    struct k_work_delayable scroll_work;
    struct inertia_pending_report move_report;
    struct inertia_pending_report scroll_report;
#if IS_ENABLED(CONFIG_ZMK_INPUT_INERTIA_TEST)
    atomic_t move_emit_count;
    atomic_t scroll_emit_count;
#endif
};

struct inertia_data {
    struct inertia_config config;
    const struct device *dev;
    struct inertia_stream streams[INERTIA_STREAM_COUNT];
    struct k_mutex lock;
};

static uint16_t inertia_abs_i16(int16_t value) {
    return value < 0 ? (uint16_t)(-(int32_t)value) : (uint16_t)value;
}

static bool ctrl_mod_is_active(void) {
    return (zmk_hid_get_keyboard_report()->body.modifiers & (MOD_LCTL | MOD_RCTL)) != 0;
}

static void clear_motion_state(struct inertia_motion_state *motion) {
    motion->velocity[INERTIA_AXIS_X] = 0;
    motion->velocity[INERTIA_AXIS_Y] = 0;
    motion->ema[INERTIA_AXIS_X] = 0;
    motion->ema[INERTIA_AXIS_Y] = 0;
    motion->remainder_q8[INERTIA_AXIS_X] = 0;
    motion->remainder_q8[INERTIA_AXIS_Y] = 0;
    motion->active = false;
    motion->inertial = false;
    inertia_frame_init(&motion->frame);
}

static bool motion_has_state(const struct inertia_motion_state *motion) {
    return motion->active || motion->frame.open || motion->velocity[INERTIA_AXIS_X] != 0 ||
           motion->velocity[INERTIA_AXIS_Y] != 0 || motion->ema[INERTIA_AXIS_X] != 0 ||
           motion->ema[INERTIA_AXIS_Y] != 0 || motion->remainder_q8[INERTIA_AXIS_X] != 0 ||
           motion->remainder_q8[INERTIA_AXIS_Y] != 0;
}

static void cancel_move_locked(struct inertia_stream *stream) {
    (void)k_work_cancel_delayable(&stream->move_work);
    atomic_inc(&stream->move.generation);
    clear_motion_state(&stream->move);
}

static void cancel_scroll_locked(struct inertia_stream *stream) {
    (void)k_work_cancel_delayable(&stream->scroll_work);
    atomic_inc(&stream->scroll.generation);
    clear_motion_state(&stream->scroll);
}

/*
 * A missing state is the single-stream call and uses stream zero. An index past
 * the allocated streams has no stream at all: its event passes through
 * untouched rather than landing on stream zero's motion.
 */
static struct inertia_stream *stream_for_event(struct inertia_data *data,
                                               const struct zmk_input_processor_state *state) {
    if (state == NULL) {
        return &data->streams[0];
    }

    if (state->input_device_index >= INERTIA_STREAM_COUNT) {
        return NULL;
    }

    return &data->streams[state->input_device_index];
}

static void begin_move_frame_locked(struct inertia_data *data, struct inertia_stream *target) {
    if (target->move.frame.open) {
        return;
    }

    for (size_t i = 0U; i < INERTIA_STREAM_COUNT; i++) {
        struct inertia_stream *stream = &data->streams[i];

        if (motion_has_state(&stream->scroll)) {
            cancel_scroll_locked(stream);
        }
        if (stream != target && motion_has_state(&stream->move)) {
            cancel_move_locked(stream);
        }
    }

    (void)k_work_cancel_delayable(&target->move_work);
    atomic_inc(&target->move.generation);
    if (target->move.inertial) {
        clear_motion_state(&target->move);
    } else {
        target->move.velocity[INERTIA_AXIS_X] = 0;
        target->move.velocity[INERTIA_AXIS_Y] = 0;
        target->move.remainder_q8[INERTIA_AXIS_X] = 0;
        target->move.remainder_q8[INERTIA_AXIS_Y] = 0;
        target->move.active = false;
        target->move.inertial = false;
    }
}

static void begin_scroll_frame_locked(struct inertia_data *data, struct inertia_stream *target) {
    if (target->scroll.frame.open) {
        return;
    }

    for (size_t i = 0U; i < INERTIA_STREAM_COUNT; i++) {
        struct inertia_stream *stream = &data->streams[i];

        if (motion_has_state(&stream->move)) {
            cancel_move_locked(stream);
        }
        if (stream != target && motion_has_state(&stream->scroll)) {
            cancel_scroll_locked(stream);
        }
    }

    (void)k_work_cancel_delayable(&target->scroll_work);
    atomic_inc(&target->scroll.generation);
    if (target->scroll.inertial) {
        clear_motion_state(&target->scroll);
    } else {
        target->scroll.velocity[INERTIA_AXIS_X] = 0;
        target->scroll.velocity[INERTIA_AXIS_Y] = 0;
        target->scroll.remainder_q8[INERTIA_AXIS_X] = 0;
        target->scroll.remainder_q8[INERTIA_AXIS_Y] = 0;
        target->scroll.active = false;
        target->scroll.inertial = false;
    }
}

static void finish_motion_frame_locked(struct inertia_motion_state *motion,
                                       struct k_work_delayable *work, uint16_t threshold_start,
                                       uint16_t trigger_ms) {
    int16_t value[INERTIA_AXIS_COUNT];

    if (!inertia_frame_finish(&motion->frame, value)) {
        return;
    }

    for (size_t axis = 0U; axis < INERTIA_AXIS_COUNT; axis++) {
        motion->velocity[axis] = value[axis];
        motion->ema[axis] = inertia_half((int32_t)value[axis] + motion->ema[axis]);
    }

    if (inertia_abs_i16(value[INERTIA_AXIS_X]) >= threshold_start ||
        inertia_abs_i16(value[INERTIA_AXIS_Y]) >= threshold_start) {
        motion->active = true;
        motion->inertial = false;
        (void)k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), work,
                                          K_MSEC(trigger_ms));
        return;
    }

    (void)k_work_cancel_delayable(work);
    clear_motion_state(motion);
}

static bool decay_motion_locked(struct inertia_motion_state *motion, uint16_t factor_q8,
                                uint16_t threshold_stop, int16_t output[INERTIA_AXIS_COUNT]) {
    inertia_decay_axis(motion->ema[INERTIA_AXIS_X], factor_q8, &output[INERTIA_AXIS_X],
                       &motion->remainder_q8[INERTIA_AXIS_X]);
    inertia_decay_axis(motion->ema[INERTIA_AXIS_Y], factor_q8, &output[INERTIA_AXIS_Y],
                       &motion->remainder_q8[INERTIA_AXIS_Y]);

    if (inertia_abs_i16(output[INERTIA_AXIS_X]) <= threshold_stop &&
        inertia_abs_i16(output[INERTIA_AXIS_Y]) <= threshold_stop) {
        clear_motion_state(motion);
        return false;
    }

    for (size_t axis = 0U; axis < INERTIA_AXIS_COUNT; axis++) {
        motion->velocity[axis] = output[axis];
        motion->ema[axis] = output[axis];
    }
    motion->inertial = true;
    return true;
}

/* Queue a single token for the complete X/Y or wheel frame. The payload stays
 * in its stream until consumed, so congestion cannot split axes or grow a
 * private backlog. Cancellation changes generation, never reuses a queued slot. */
static void decay_work(struct inertia_stream *stream, bool scroll) {
    struct inertia_data *data = stream->owner;
    const struct inertia_config *config = &data->config;
    struct inertia_motion_state *motion = scroll ? &stream->scroll : &stream->move;
    struct inertia_pending_report *pending = scroll ? &stream->scroll_report : &stream->move_report;
    struct k_work_delayable *work = scroll ? &stream->scroll_work : &stream->move_work;
    k_mutex_lock(&data->lock, K_FOREVER);
    uint16_t interval = scroll ? config->scroll_interval_ms : config->move_interval_ms;
    uint16_t factor = scroll ? config->scroll_decay_factor_q8 : config->move_decay_factor_q8;
    uint16_t threshold = scroll ? config->scroll_threshold_stop : config->move_threshold_stop;

    if (!motion->active) {
        k_mutex_unlock(&data->lock);
        return;
    }
    if (scroll && config->cancel_scroll_inertia_on_ctrl && ctrl_mod_is_active()) {
        cancel_scroll_locked(stream);
        k_mutex_unlock(&data->lock);
        return;
    }
    if (pending->queued) {
        (void)k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), work, K_MSEC(interval));
        k_mutex_unlock(&data->lock);
        return;
    }
    if (!decay_motion_locked(motion, factor, threshold, pending->output)) {
        k_mutex_unlock(&data->lock);
        return;
    }

    pending->generation = (uint32_t)atomic_get(&motion->generation);
    pending->queued = true;
    uint16_t code = (uint16_t)(2U * (size_t)(stream - data->streams) + scroll);
    int err = input_report(data->dev, INPUT_EV_MSC, code, (int32_t)pending->generation,
                           true, K_NO_WAIT);
    if (err != 0) {
        pending->queued = false;
    }
    (void)k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), work, K_MSEC(interval));
    k_mutex_unlock(&data->lock);
    if (err != 0) {
        LOG_WRN_ONCE("inertia input queue full: rc=%d; repeated failures suppressed", err);
    }
}

static void move_decay_callback(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct inertia_stream *stream = CONTAINER_OF(dwork, struct inertia_stream, move_work);
    decay_work(stream, false);
}

static void scroll_decay_callback(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct inertia_stream *stream = CONTAINER_OF(dwork, struct inertia_stream, scroll_work);
    decay_work(stream, true);
}

/* Only this node's internal tokens reach this callback. It runs on Zephyr's
 * input thread, alongside unmodified ZMK input listeners, so their complete
 * HID set/send/clear transactions cannot interleave with inertia transactions.
 * State locks are released before calling the potentially blocking endpoint. */
static void inertia_input_callback(struct input_event *event, void *user_data) {
    ARG_UNUSED(user_data);
    if (event->type != INPUT_EV_MSC || event->code >= 2U * INERTIA_STREAM_COUNT) {
        return;
    }
    struct inertia_data *data = event->dev->data;
    size_t index = event->code / 2U;
    bool scroll = (event->code & 1U) != 0U;
    struct inertia_stream *stream = &data->streams[index];
    struct inertia_motion_state *motion = scroll ? &stream->scroll : &stream->move;
    struct inertia_pending_report *pending = scroll ? &stream->scroll_report : &stream->move_report;
    const struct inertia_config *config = &data->config;
    int16_t output[INERTIA_AXIS_COUNT];
#if IS_ENABLED(CONFIG_ZMK_INPUT_INERTIA_TEST)
    inertia_test_before_emit(data->dev, index, scroll);
#endif
    k_mutex_lock(&data->lock, K_FOREVER);
    if (!pending->queued || pending->generation != (uint32_t)event->value) {
        k_mutex_unlock(&data->lock);
        return;
    }
    pending->queued = false;
    if (pending->generation != (uint32_t)atomic_get(&motion->generation) || !motion->active ||
        !motion->inertial) {
        k_mutex_unlock(&data->lock);
        return;
    }
    if (scroll && config->cancel_scroll_inertia_on_ctrl && ctrl_mod_is_active()) {
        cancel_scroll_locked(stream);
        k_mutex_unlock(&data->lock);
        return;
    }
    output[INERTIA_AXIS_X] = pending->output[INERTIA_AXIS_X];
    output[INERTIA_AXIS_Y] = pending->output[INERTIA_AXIS_Y];
#if IS_ENABLED(CONFIG_ZMK_INPUT_INERTIA_TEST)
    atomic_inc(scroll ? &stream->scroll_emit_count : &stream->move_emit_count);
#endif
    k_mutex_unlock(&data->lock);

    if (scroll) {
        zmk_hid_mouse_scroll_set(output[INERTIA_AXIS_X], output[INERTIA_AXIS_Y]);
    } else {
        zmk_hid_mouse_movement_set(output[INERTIA_AXIS_X], output[INERTIA_AXIS_Y]);
    }
    const int err = zmk_endpoint_send_mouse_report();
    if (scroll) {
        zmk_hid_mouse_scroll_set(0, 0);
    } else {
        zmk_hid_mouse_movement_set(0, 0);
    }
    if (err != 0) {
        LOG_WRN_ONCE("inertia report failed: rc=%d; repeated failures suppressed", err);
    }
}

static int inertia_handle_event(const struct device *dev, struct input_event *event,
                                uint32_t param1, uint32_t param2,
                                struct zmk_input_processor_state *state) {
    struct inertia_data *data = dev->data;
    const struct inertia_config *config = &data->config;
    struct inertia_stream *stream = stream_for_event(data, state);
    bool move_x = event->type == INPUT_EV_REL && event->code == INPUT_REL_X;
    bool move_y = event->type == INPUT_EV_REL && event->code == INPUT_REL_Y;
    bool scroll_x = event->type == INPUT_EV_REL && event->code == INPUT_REL_HWHEEL;
    bool scroll_y = event->type == INPUT_EV_REL && event->code == INPUT_REL_WHEEL;

    ARG_UNUSED(param1);
    ARG_UNUSED(param2);

    if (stream == NULL || (!move_x && !move_y && !scroll_x && !scroll_y && !event->sync)) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    k_mutex_lock(&data->lock, K_FOREVER);

    if (move_x || move_y) {
        begin_move_frame_locked(data, stream);
        inertia_frame_add(&stream->move.frame, move_x ? INERTIA_AXIS_X : INERTIA_AXIS_Y,
                          event->value);
    } else if (scroll_x || scroll_y) {
        if (config->cancel_scroll_inertia_on_ctrl && ctrl_mod_is_active()) {
            for (size_t i = 0U; i < INERTIA_STREAM_COUNT; i++) {
                if (motion_has_state(&data->streams[i].move)) {
                    cancel_move_locked(&data->streams[i]);
                }
                if (motion_has_state(&data->streams[i].scroll)) {
                    cancel_scroll_locked(&data->streams[i]);
                }
            }
            k_mutex_unlock(&data->lock);
            return ZMK_INPUT_PROC_CONTINUE;
        }

        begin_scroll_frame_locked(data, stream);
        inertia_frame_add(&stream->scroll.frame, scroll_x ? INERTIA_AXIS_X : INERTIA_AXIS_Y,
                          event->value);
    }

    if (event->sync) {
        finish_motion_frame_locked(&stream->move, &stream->move_work, config->move_threshold_start,
                                   config->trigger_ms);
        finish_motion_frame_locked(&stream->scroll, &stream->scroll_work,
                                   config->scroll_threshold_start, config->trigger_ms);
    }

    k_mutex_unlock(&data->lock);
    return ZMK_INPUT_PROC_CONTINUE;
}

static int inertia_init(const struct device *dev) {
    struct inertia_data *data = dev->data;

    data->dev = dev;
    data->config = *(const struct inertia_config *)dev->config;
    k_mutex_init(&data->lock);

    for (size_t i = 0U; i < INERTIA_STREAM_COUNT; i++) {
        struct inertia_stream *stream = &data->streams[i];

        stream->owner = data;
        clear_motion_state(&stream->move);
        clear_motion_state(&stream->scroll);
        atomic_set(&stream->move.generation, 0);
        atomic_set(&stream->scroll.generation, 0);
        stream->move_report = (struct inertia_pending_report){0};
        stream->scroll_report = (struct inertia_pending_report){0};
#if IS_ENABLED(CONFIG_ZMK_INPUT_INERTIA_TEST)
        atomic_set(&stream->move_emit_count, 0);
        atomic_set(&stream->scroll_emit_count, 0);
#endif
        k_work_init_delayable(&stream->move_work, move_decay_callback);
        k_work_init_delayable(&stream->scroll_work, scroll_decay_callback);
    }

    return 0;
}

static const struct zmk_input_processor_driver_api inertia_driver_api = {
    .handle_event = inertia_handle_event,
};

static bool inertia_device_valid(const struct device *dev) {
    return dev != NULL && dev->api == &inertia_driver_api && device_is_ready(dev);
}
int inertia_get_params(const struct device *dev, struct inertia_params *out) {
    if (out == NULL) { return -EINVAL; }
    if (!inertia_device_valid(dev)) { return -ENODEV; }
    struct inertia_data *data = dev->data;
    k_mutex_lock(&data->lock, K_FOREVER);
    const struct inertia_config *c = &data->config;
    *out = (struct inertia_params){
        .trigger_ms = c->trigger_ms,
        .move_decay_factor_int = (c->move_decay_factor_q8 * 100 + 255) / 256,
        .scroll_decay_factor_int = (c->scroll_decay_factor_q8 * 100 + 255) / 256,
        .move_report_interval_ms = c->move_interval_ms,
        .scroll_report_interval_ms = c->scroll_interval_ms,
        .move_threshold_start = c->move_threshold_start, .move_threshold_stop = c->move_threshold_stop,
        .scroll_threshold_start = c->scroll_threshold_start, .scroll_threshold_stop = c->scroll_threshold_stop,
        .cancel_scroll_inertia_on_ctrl = c->cancel_scroll_inertia_on_ctrl,
    };
    k_mutex_unlock(&data->lock);
    return 0;
}
int inertia_set_params(const struct device *dev, const struct inertia_params *params) {
    if (params == NULL) { return -EINVAL; }
    if (!inertia_device_valid(dev)) { return -ENODEV; }
#define VALIDATE(n, field, key, lo, hi) if (params->field < (lo) || params->field > (hi)) { return -EINVAL; }
    INERTIA_INT_FIELDS(VALIDATE, 0)
#undef VALIDATE
    if (params->move_threshold_stop >= params->move_threshold_start ||
        params->scroll_threshold_stop >= params->scroll_threshold_start) { return -EINVAL; }
    const struct inertia_config next = {
        .trigger_ms = params->trigger_ms,
        .move_decay_factor_q8 = INERTIA_FACTOR_Q8(params->move_decay_factor_int),
        .scroll_decay_factor_q8 = INERTIA_FACTOR_Q8(params->scroll_decay_factor_int),
        .move_interval_ms = params->move_report_interval_ms,
        .scroll_interval_ms = params->scroll_report_interval_ms,
        .move_threshold_start = params->move_threshold_start, .move_threshold_stop = params->move_threshold_stop,
        .scroll_threshold_start = params->scroll_threshold_start, .scroll_threshold_stop = params->scroll_threshold_stop,
        .cancel_scroll_inertia_on_ctrl = params->cancel_scroll_inertia_on_ctrl,
    };
    struct inertia_data *data = dev->data;
    k_mutex_lock(&data->lock, K_FOREVER);
    const struct inertia_config *c = &data->config;
    bool same = c->trigger_ms == next.trigger_ms &&
        c->move_decay_factor_q8 == next.move_decay_factor_q8 && c->scroll_decay_factor_q8 == next.scroll_decay_factor_q8 &&
        c->move_interval_ms == next.move_interval_ms && c->scroll_interval_ms == next.scroll_interval_ms &&
        c->move_threshold_start == next.move_threshold_start && c->move_threshold_stop == next.move_threshold_stop &&
        c->scroll_threshold_start == next.scroll_threshold_start && c->scroll_threshold_stop == next.scroll_threshold_stop &&
        c->cancel_scroll_inertia_on_ctrl == next.cancel_scroll_inertia_on_ctrl;
    if (!same) {
        data->config = next;
        for (size_t i = 0; i < INERTIA_STREAM_COUNT; i++) {
            cancel_move_locked(&data->streams[i]);
            cancel_scroll_locked(&data->streams[i]);
        }
    }
    k_mutex_unlock(&data->lock);
    return 0;
}

#define INERTIA_INST(n)                                                                            \
    BUILD_ASSERT(DT_INST_PROP(n, move_decay_factor_int) >= 0 &&                                    \
                     DT_INST_PROP(n, move_decay_factor_int) <= 99,                                 \
                 "move-decay-factor-int must be between 0 and 99");                                \
    BUILD_ASSERT(DT_INST_PROP(n, scroll_decay_factor_int) >= 0 &&                                  \
                     DT_INST_PROP(n, scroll_decay_factor_int) <= 99,                               \
                 "scroll-decay-factor-int must be between 0 and 99");                              \
    BUILD_ASSERT(DT_INST_PROP(n, move_report_interval_ms) > 0 &&                                   \
                     DT_INST_PROP(n, move_report_interval_ms) <= UINT16_MAX,                       \
                 "move-report-interval-ms must be between 1 and 65535");                           \
    BUILD_ASSERT(DT_INST_PROP(n, scroll_report_interval_ms) > 0 &&                                 \
                     DT_INST_PROP(n, scroll_report_interval_ms) <= UINT16_MAX,                     \
                 "scroll-report-interval-ms must be between 1 and 65535");                         \
    BUILD_ASSERT(DT_INST_PROP(n, trigger_ms) > 0 && DT_INST_PROP(n, trigger_ms) <= UINT16_MAX,     \
                 "trigger-ms must be between 1 and 65535");                                        \
    BUILD_ASSERT(DT_INST_PROP(n, move_threshold_start) > 0 &&                                      \
                     DT_INST_PROP(n, move_threshold_start) <= INT16_MAX,                           \
                 "move-threshold-start must be between 1 and 32767");                              \
    BUILD_ASSERT(DT_INST_PROP(n, move_threshold_stop) >= 0 &&                                      \
                     DT_INST_PROP(n, move_threshold_stop) < DT_INST_PROP(n, move_threshold_start), \
                 "move-threshold-stop must be less than move-threshold-start");                    \
    BUILD_ASSERT(DT_INST_PROP(n, scroll_threshold_start) > 0 &&                                    \
                     DT_INST_PROP(n, scroll_threshold_start) <= INT16_MAX,                         \
                 "scroll-threshold-start must be between 1 and 32767");                            \
    BUILD_ASSERT(DT_INST_PROP(n, scroll_threshold_stop) >= 0 &&                                    \
                     DT_INST_PROP(n, scroll_threshold_stop) <                                      \
                         DT_INST_PROP(n, scroll_threshold_start),                                  \
                 "scroll-threshold-stop must be less than scroll-threshold-start");                \
    static const struct inertia_config processor_inertia_config_##n = {                            \
        .move_decay_factor_q8 = INERTIA_FACTOR_Q8(DT_INST_PROP(n, move_decay_factor_int)),         \
        .move_interval_ms = DT_INST_PROP(n, move_report_interval_ms),                              \
        .move_threshold_start = DT_INST_PROP(n, move_threshold_start),                             \
        .move_threshold_stop = DT_INST_PROP(n, move_threshold_stop),                               \
        .scroll_decay_factor_q8 = INERTIA_FACTOR_Q8(DT_INST_PROP(n, scroll_decay_factor_int)),     \
        .scroll_interval_ms = DT_INST_PROP(n, scroll_report_interval_ms),                          \
        .scroll_threshold_start = DT_INST_PROP(n, scroll_threshold_start),                         \
        .scroll_threshold_stop = DT_INST_PROP(n, scroll_threshold_stop),                           \
        .trigger_ms = DT_INST_PROP(n, trigger_ms),                                                 \
        .cancel_scroll_inertia_on_ctrl = DT_INST_PROP(n, cancel_scroll_inertia_on_ctrl),           \
    };                                                                                             \
    static struct inertia_data processor_inertia_data_##n;                                         \
    DEVICE_DT_INST_DEFINE(n, inertia_init, NULL, &processor_inertia_data_##n,                      \
                          &processor_inertia_config_##n, POST_KERNEL,                              \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &inertia_driver_api);                                                   \
    static void inertia_input_callback_##n(struct input_event *event, void *user_data) {             \
        inertia_input_callback(event, user_data);                                                   \
    }                                                                                              \
    INPUT_CALLBACK_DEFINE(DEVICE_DT_INST_GET(n), inertia_input_callback_##n, NULL);

DT_INST_FOREACH_STATUS_OKAY(INERTIA_INST)

#if IS_ENABLED(CONFIG_ZMK_INPUT_INERTIA_TEST)
__weak void inertia_test_before_emit(const struct device *dev, size_t stream_index, bool scroll) {
    ARG_UNUSED(dev);
    ARG_UNUSED(stream_index);
    ARG_UNUSED(scroll);
}

int inertia_test_get_snapshot(const struct device *dev, size_t stream_index, bool scroll,
                              struct inertia_test_snapshot *snapshot) {
    if (dev == NULL || snapshot == NULL || stream_index >= INERTIA_STREAM_COUNT) {
        return -EINVAL;
    }

    struct inertia_data *data = dev->data;
    struct inertia_stream *stream = &data->streams[stream_index];
    struct inertia_motion_state *motion = scroll ? &stream->scroll : &stream->move;

    k_mutex_lock(&data->lock, K_FOREVER);
    snapshot->velocity[INERTIA_AXIS_X] = motion->velocity[INERTIA_AXIS_X];
    snapshot->velocity[INERTIA_AXIS_Y] = motion->velocity[INERTIA_AXIS_Y];
    snapshot->ema[INERTIA_AXIS_X] = motion->ema[INERTIA_AXIS_X];
    snapshot->ema[INERTIA_AXIS_Y] = motion->ema[INERTIA_AXIS_Y];
    snapshot->generation = (uint32_t)atomic_get(&motion->generation);
    snapshot->emit_count =
        (uint32_t)atomic_get(scroll ? &stream->scroll_emit_count : &stream->move_emit_count);
    snapshot->active = motion->active;
    snapshot->inertial = motion->inertial;
    snapshot->frame_open = motion->frame.open;
    snapshot->report_pending = (scroll ? &stream->scroll_report : &stream->move_report)->queued;
    k_mutex_unlock(&data->lock);

    return 0;
}
#endif
