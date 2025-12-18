/*
 * Copyright (c) 2025 @amgskobo
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_inertia

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <drivers/input_processor.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(input_inertia, CONFIG_ZMK_LOG_LEVEL);

#define DEFAULT_INERTIA_DECAY_FACTOR_INT 90
#define DEFAULT_INERTIA_INTERVAL_MS 35
#define DEFAULT_INERTIA_THRESHOLD_START 15
#define DEFAULT_INERTIA_THRESHOLD_STOP 1

#define DEFAULT_INERTIA_SCROLL_DECAY_FACTOR_INT 85
#define DEFAULT_INERTIA_SCROLL_INTERVAL_MS 65
#define DEFAULT_INERTIA_SCROLL_THRESHOLD_START 2
#define DEFAULT_INERTIA_SCROLL_THRESHOLD_STOP 0

#define abs16(x) ((x) < 0 ? -(x) : (x))

struct inertia_config {
    // Mouse movement config
    uint16_t move_decay_factor_int;
    uint16_t move_interval_ms;
    uint16_t move_threshold_start;
    uint16_t move_threshold_stop;

    // Scroll config
    uint16_t scroll_decay_factor_int;
    uint16_t scroll_interval_ms;
    uint16_t scroll_threshold_start;
    uint16_t scroll_threshold_stop;
};

struct inertia_state {
    // Mouse movement state
    int16_t move_vx;
    int16_t move_vy;
    int16_t move_remainder_x_q8;
    int16_t move_remainder_y_q8;
    bool move_active;
    bool move_is_inertial;

    // Scroll state
    int16_t scroll_vx;
    int16_t scroll_vy;
    int16_t scroll_remainder_x_q8;
    int16_t scroll_remainder_y_q8;
    bool scroll_active;
    bool scroll_is_inertial;
};

struct inertia_data {
    const struct device *dev;
    struct inertia_state state;
    struct k_work_delayable move_work;
    struct k_work_delayable scroll_work;
    struct k_mutex lock;
};

// ====================================================================
// Q8 Fixed-Point Decay Function
// ====================================================================
// Q8 Definitions
#define Q8_VALUE (1 << 8) // 1.0 = 256
#define Q8_HALF (1 << 7)  // 0.5 = 128

void calculate_decayed_movement_fixed(int16_t in_dx, int16_t in_dy, int16_t decay_factor_q8,
                                      int16_t *out_dx, int16_t *out_dy, int16_t *rem_x,
                                      int16_t *rem_y) {
    // 1. True Movement (Q8, including remainder)
    int32_t ideal_dx_q8 = ((int32_t)in_dx << 8) + *rem_x;
    int32_t ideal_dy_q8 = ((int32_t)in_dy << 8) + *rem_y;

    // 2. Apply Decay
    int32_t decayed_dx_q8 = (ideal_dx_q8 * decay_factor_q8) >> 8;
    int32_t decayed_dy_q8 = (ideal_dy_q8 * decay_factor_q8) >> 8;

    // 3. Extract Integer part (Output values)
    int16_t output_dx = (int16_t)((decayed_dx_q8 + Q8_HALF) >> 8);
    int16_t output_dy = (int16_t)((decayed_dy_q8 + Q8_HALF) >> 8);

    // 4. Update and save Remainder (Q8 value)
    *rem_x = (int16_t)(decayed_dx_q8 - ((int32_t)output_dx << 8));
    *rem_y = (int16_t)(decayed_dy_q8 - ((int32_t)output_dy << 8));

    *out_dx = output_dx;
    *out_dy = output_dy;
}
// ====================================================================

static void move_decay_callback(struct k_work *work) {
    struct k_work_delayable *d_work = k_work_delayable_from_work(work);
    struct inertia_data *data = CONTAINER_OF(d_work, struct inertia_data, move_work);
    const struct inertia_config *cfg = data->dev->config;

    k_mutex_lock(&data->lock, K_FOREVER);
    if (!data->state.move_active) {
        k_mutex_unlock(&data->lock);
        return;
    }

    int16_t vx = data->state.move_vx;
    int16_t vy = data->state.move_vy;
    int16_t next_vx = vx;
    int16_t next_vy = vy;

    // Q8 Factor Scaling
    int16_t decay_factor_q8 = (cfg->move_decay_factor_int * 256) / 100;

    // STEP 1: Q8 Fixed-Point Decay
    calculate_decayed_movement_fixed(vx, vy, decay_factor_q8, &next_vx, &next_vy,
                                     &data->state.move_remainder_x_q8,
                                     &data->state.move_remainder_y_q8);

    // STEP 2: Termination Check
    if (abs16(next_vx) <= cfg->move_threshold_stop && abs16(next_vy) <= cfg->move_threshold_stop) {
        data->state.move_vx = 0;
        data->state.move_vy = 0;
        data->state.move_remainder_x_q8 = 0;
        data->state.move_remainder_y_q8 = 0;
        data->state.move_active = false;
        data->state.move_is_inertial = false;
        k_mutex_unlock(&data->lock);

        zmk_hid_mouse_movement_set(0, 0);
        zmk_endpoints_send_mouse_report();
        LOG_DBG("Move Inertia stopped naturally.");
        return;
    }

    // Continue inertia
    data->state.move_vx = next_vx;
    data->state.move_vy = next_vy;
    data->state.move_is_inertial = true;

    zmk_hid_mouse_movement_set(next_vx, next_vy);
    zmk_endpoints_send_mouse_report();

    k_work_reschedule(&data->move_work, K_MSEC(cfg->move_interval_ms));
    k_mutex_unlock(&data->lock);
}

static void scroll_decay_callback(struct k_work *work) {
    struct k_work_delayable *d_work = k_work_delayable_from_work(work);
    struct inertia_data *data = CONTAINER_OF(d_work, struct inertia_data, scroll_work);
    const struct inertia_config *cfg = data->dev->config;

    k_mutex_lock(&data->lock, K_FOREVER);
    if (!data->state.scroll_active) {
        k_mutex_unlock(&data->lock);
        return;
    }

    int16_t vx = data->state.scroll_vx;
    int16_t vy = data->state.scroll_vy;
    int16_t next_vx = vx;
    int16_t next_vy = vy;

    // Q8 Factor Scaling
    int16_t decay_factor_q8 = (cfg->scroll_decay_factor_int * 256) / 100;

    // STEP 1: Q8 Fixed-Point Decay
    calculate_decayed_movement_fixed(vx, vy, decay_factor_q8, &next_vx, &next_vy,
                                     &data->state.scroll_remainder_x_q8,
                                     &data->state.scroll_remainder_y_q8);

    // STEP 2: Termination Check
    if (abs16(next_vx) <= cfg->scroll_threshold_stop &&
        abs16(next_vy) <= cfg->scroll_threshold_stop) {
        data->state.scroll_vx = 0;
        data->state.scroll_vy = 0;
        data->state.scroll_remainder_x_q8 = 0;
        data->state.scroll_remainder_y_q8 = 0;
        data->state.scroll_active = false;
        data->state.scroll_is_inertial = false;
        k_mutex_unlock(&data->lock);

        zmk_hid_mouse_scroll_set(0, 0);
        zmk_endpoints_send_mouse_report();
        LOG_DBG("Scroll Inertia stopped naturally.");
        return;
    }

    // Continue inertia
    data->state.scroll_vx = next_vx;
    data->state.scroll_vy = next_vy;
    data->state.scroll_is_inertial = true;

    zmk_hid_mouse_scroll_set(next_vx, next_vy);
    zmk_endpoints_send_mouse_report();

    k_work_reschedule(&data->scroll_work, K_MSEC(cfg->scroll_interval_ms));
    k_mutex_unlock(&data->lock);
}

/* --- Input Processor Handler (Event-Driven Pipeline) --- */

static int inertia_handle_event(const struct device *dev, struct input_event *event,
                                uint32_t param1, uint32_t param2,
                                struct zmk_input_processor_state *state) {
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);
    ARG_UNUSED(state);

    if (event->type != INPUT_EV_REL) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    struct inertia_data *data = (struct inertia_data *)dev->data;
    const struct inertia_config *cfg = dev->config;

    // --- MOUSE MOVEMENT ---
    if (event->code == INPUT_REL_X || event->code == INPUT_REL_Y) {
        int16_t val = (int16_t)event->value;
        if (val == 0) return ZMK_INPUT_PROC_CONTINUE;

        k_mutex_lock(&data->lock, K_FOREVER);

        // Transition: If background inertia is running, clear it for a fresh manual start.
        // We check move_is_inertial to prevent resetting X when the Y event of the same packet arrives.
        if (data->state.move_active && data->state.move_is_inertial) {
            k_work_cancel_delayable(&data->move_work);
            data->state.move_active = false;
            data->state.move_vx = 0;
            data->state.move_vy = 0;
            data->state.move_remainder_x_q8 = 0;
            data->state.move_remainder_y_q8 = 0;
            data->state.move_is_inertial = false;
        }
        // Also cancel scroll inertia to prevent conflict
        if (data->state.scroll_active) {
            k_work_cancel_delayable(&data->scroll_work);
            data->state.scroll_active = false;
            data->state.scroll_vx = 0;
            data->state.scroll_vy = 0;
            data->state.scroll_remainder_x_q8 = 0;
            data->state.scroll_remainder_y_q8 = 0;
            data->state.scroll_is_inertial = false;
        }

        if (event->code == INPUT_REL_X) data->state.move_vx = val;
        if (event->code == INPUT_REL_Y) data->state.move_vy = val;

        // Manual movement is NOT marked as is_inertial yet.
        // It becomes is_inertial only when the background timer takes over.
        if (abs16(data->state.move_vx) >= cfg->move_threshold_start ||
            abs16(data->state.move_vy) >= cfg->move_threshold_start) {
            data->state.move_active = true;
            data->state.move_is_inertial = false; 
            k_work_reschedule(&data->move_work, K_MSEC(cfg->move_interval_ms));
            LOG_DBG("Move Inertia triggered. x %d, y %d", data->state.move_vx,
                    data->state.move_vy);
        }
        k_mutex_unlock(&data->lock);
    }
    // --- SCROLLING ---
    else if (event->code == INPUT_REL_WHEEL || event->code == INPUT_REL_HWHEEL) {
        int16_t val = (int16_t)event->value;
        //if (val == 0) return ZMK_INPUT_PROC_CONTINUE;

        k_mutex_lock(&data->lock, K_FOREVER);

        if (data->state.scroll_active && data->state.scroll_is_inertial) {
            k_work_cancel_delayable(&data->scroll_work);
            data->state.scroll_active = false;
            data->state.scroll_vx = 0;
            data->state.scroll_vy = 0;
            data->state.scroll_remainder_x_q8 = 0;
            data->state.scroll_remainder_y_q8 = 0;
            data->state.scroll_is_inertial = false;
        }
        // Also cancel move inertia to prevent conflict
        if (data->state.move_active) {
            k_work_cancel_delayable(&data->move_work);
            data->state.move_active = false;
            data->state.move_vx = 0;
            data->state.move_vy = 0;
            data->state.move_remainder_x_q8 = 0;
            data->state.move_remainder_y_q8 = 0;
            data->state.move_is_inertial = false;
        }

        if (event->code == INPUT_REL_HWHEEL) data->state.scroll_vx = val;
        if (event->code == INPUT_REL_WHEEL) data->state.scroll_vy = val;

        if (abs16(data->state.scroll_vx) >= cfg->scroll_threshold_start ||
            abs16(data->state.scroll_vy) >= cfg->scroll_threshold_start) {
            data->state.scroll_active = true;
            data->state.scroll_is_inertial = false;
            k_work_reschedule(&data->scroll_work, K_MSEC(cfg->scroll_interval_ms));
            LOG_DBG("Scroll Inertia triggered. h %d, v %d", data->state.scroll_vx,
                    data->state.scroll_vy);
        }
        k_mutex_unlock(&data->lock);
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static int inertia_init(const struct device *dev) {
    struct inertia_data *data = (struct inertia_data *)dev->data;
    data->dev = dev;

    // Reset move state
    data->state.move_vx = 0;
    data->state.move_vy = 0;
    data->state.move_remainder_x_q8 = 0;
    data->state.move_remainder_y_q8 = 0;
    data->state.move_active = false;
    data->state.move_is_inertial = false;

    // Reset scroll state
    data->state.scroll_vx = 0;
    data->state.scroll_vy = 0;
    data->state.scroll_remainder_x_q8 = 0;
    data->state.scroll_remainder_y_q8 = 0;
    data->state.scroll_active = false;
    data->state.scroll_is_inertial = false;

    k_mutex_init(&data->lock);
    k_work_init_delayable(&data->move_work, move_decay_callback);
    k_work_init_delayable(&data->scroll_work, scroll_decay_callback);
    return 0;
}

/* Driver API */
static const struct zmk_input_processor_driver_api inertia_driver_api = {
    .handle_event = inertia_handle_event,
};

#define INERTIA_INST(n)                                                                            \
    static struct inertia_data processor_inertia_data_##n = {};                                    \
    static const struct inertia_config processor_inertia_config_##n = {                            \
        .move_decay_factor_int =                                                                   \
            DT_INST_PROP_OR(n, decay_factor_int, DEFAULT_INERTIA_DECAY_FACTOR_INT),                \
        .move_interval_ms = DT_INST_PROP_OR(n, report_interval_ms, DEFAULT_INERTIA_INTERVAL_MS),   \
        .move_threshold_start =                                                                    \
            DT_INST_PROP_OR(n, threshold_start, DEFAULT_INERTIA_THRESHOLD_START),                  \
        .move_threshold_stop = DT_INST_PROP_OR(n, threshold_stop, DEFAULT_INERTIA_THRESHOLD_STOP), \
                                                                                                   \
        .scroll_decay_factor_int =                                                                 \
            DT_INST_PROP_OR(n, scroll_decay_factor_int, DEFAULT_INERTIA_SCROLL_DECAY_FACTOR_INT),  \
        .scroll_interval_ms =                                                                      \
            DT_INST_PROP_OR(n, scroll_report_interval_ms, DEFAULT_INERTIA_SCROLL_INTERVAL_MS),     \
        .scroll_threshold_start =                                                                  \
            DT_INST_PROP_OR(n, scroll_threshold_start, DEFAULT_INERTIA_SCROLL_THRESHOLD_START),    \
        .scroll_threshold_stop =                                                                   \
            DT_INST_PROP_OR(n, scroll_threshold_stop, DEFAULT_INERTIA_SCROLL_THRESHOLD_STOP),      \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, inertia_init, NULL, &processor_inertia_data_##n,                      \
                          &processor_inertia_config_##n, POST_KERNEL,                              \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &inertia_driver_api);

DT_INST_FOREACH_STATUS_OKAY(INERTIA_INST)