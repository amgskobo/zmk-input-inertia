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

#define abs16(x) ((x) < 0 ? -(x) : (x))

struct inertia_config
{
    uint16_t decay_factor_int;
    uint16_t interval_ms;
    uint16_t threshold_start;
    uint16_t threshold_stop;
    bool scroll_mode;
};

struct inertia_state
{
    int16_t current_vx;
    int16_t current_vy;
    int16_t remainder_x_q8;
    int16_t remainder_y_q8;
    bool is_moving;
};

struct inertia_data
{
    const struct device *dev;
    struct inertia_state state;
    struct k_work_delayable inertia_decay_work;
    struct k_mutex lock;
};

// ====================================================================
// Q8 Fixed-Point Decay Function
// ====================================================================
// Q8 Definitions
#define Q8_VALUE (1 << 8) // 1.0 = 256
#define Q8_HALF (1 << 7)  // 0.5 = 128

void calculate_decayed_movement_fixed(int16_t in_dx, int16_t in_dy,
                                      int16_t decay_factor_q8,
                                      int16_t *out_dx, int16_t *out_dy,
                                      int16_t *rem_x, int16_t *rem_y)
{
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

static void inertia_decay_callback(struct k_work *work)
{
    // Retrieve the original device data structure from the k_work
    struct k_work_delayable *d_work = k_work_delayable_from_work(work);
    struct inertia_data *data = CONTAINER_OF(d_work, struct inertia_data, inertia_decay_work);
    const struct inertia_config *cfg = data->dev->config;

    k_mutex_lock(&data->lock, K_FOREVER);
    // Check stopping condition
    int16_t vx = data->state.current_vx;
    int16_t vy = data->state.current_vy;
    k_mutex_unlock(&data->lock);

    int16_t next_vx = vx;
    int16_t next_vy = vy;

    // 1. Q8 Factor Scaling (0-100 -> 0-256)
    int16_t decay_factor_q8 = (cfg->decay_factor_int * 256) / 100;

    // -----------------------------------------------------------------
    // STEP 1: Q8 Fixed-Point Decay (WITH Remainder Accumulation)
    calculate_decayed_movement_fixed(vx, vy, decay_factor_q8, &next_vx, &next_vy,
                                     &data->state.remainder_x_q8, &data->state.remainder_y_q8);
    // -----------------------------------------------------------------
    // STEP 2: Termination and State Update
    if (abs16(next_vx) <= cfg->threshold_stop && abs16(next_vy) <= cfg->threshold_stop)
    {
        k_mutex_lock(&data->lock, K_FOREVER);
        // Velocity is below the threshold, stop inertia motion
        data->state.current_vx = 0;
        data->state.current_vy = 0;
        k_mutex_unlock(&data->lock);
        if (cfg->scroll_mode)
        {
            zmk_hid_mouse_scroll_set(0, 0);
        }
        else
        {
            zmk_hid_mouse_movement_set(0, 0);
        }
        zmk_endpoints_send_mouse_report();
        LOG_DBG("Inertia stopped naturally. threshold_stop: %d", cfg->threshold_stop);
        return;
    }
    // Continue inertia
    k_mutex_lock(&data->lock, K_FOREVER);
    data->state.current_vx = next_vx;
    data->state.current_vy = next_vy;
    k_mutex_unlock(&data->lock);
    // Send HID report
    if (cfg->scroll_mode)
    {
        zmk_hid_mouse_scroll_set(next_vx, next_vy);
    }
    else
    {
        zmk_hid_mouse_movement_set(next_vx, next_vy);
    }
    zmk_endpoints_send_mouse_report();

    // Schedule the next update
    k_work_reschedule(&data->inertia_decay_work, K_MSEC(cfg->interval_ms));
}

/* --- Input Processor Handler (Event-Driven Pipeline) --- */

static int inertia_handle_event(const struct device *dev, struct input_event *event,
                                uint32_t param1, uint32_t param2,
                                struct zmk_input_processor_state *state)
{
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);
    ARG_UNUSED(state);

    int16_t current_dx = 0;
    int16_t current_dy = 0;
    // Process only pointer input (REL) events
    if (event->type != INPUT_EV_REL)
    {
        return ZMK_INPUT_PROC_CONTINUE;
    }
    if ((event->code == INPUT_REL_X) || (event->code == INPUT_REL_HWHEEL))
    {
        current_dx = (int16_t)event->value;
    }
    else if ((event->code == INPUT_REL_Y) || (event->code == INPUT_REL_WHEEL))
    {
        current_dy = (int16_t)event->value;
    }
    else
    {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    struct inertia_data *data = (struct inertia_data *)dev->data;
    const struct inertia_config *cfg = dev->config;

    if (data->state.is_moving)
    {
        k_work_cancel_delayable(&data->inertia_decay_work);
        data->state.is_moving = false;
        LOG_DBG("Inertia cancelled by new input.");
    }

    k_mutex_lock(&data->lock, K_FOREVER);
    if (current_dx != 0)
    {
        data->state.current_vx = current_dx;
    }

    if (current_dy != 0)
    {
        data->state.current_vy = current_dy;
    }

    if ((abs16(data->state.current_vx) >= cfg->threshold_start || abs16(data->state.current_vy) >= cfg->threshold_start))
    {
        data->state.is_moving = true;
        k_work_reschedule(&data->inertia_decay_work, K_MSEC(cfg->interval_ms));
        LOG_DBG("Inertia triggered. interval: (%dms) Initial speed x %d, y %d", cfg->interval_ms, data->state.current_vx, data->state.current_vy);
    }
    k_mutex_unlock(&data->lock);
    return ZMK_INPUT_PROC_CONTINUE;
}

static int inertia_init(const struct device *dev)
{
    struct inertia_data *data = (struct inertia_data *)dev->data;
    data->dev = dev;
    data->state.current_vx = 0;
    data->state.current_vy = 0;
    data->state.remainder_x_q8 = 0;
    data->state.remainder_y_q8 = 0;
    data->state.is_moving = false;
    k_mutex_init(&data->lock);
    k_work_init_delayable(&data->inertia_decay_work, inertia_decay_callback);
    return 0;
}

/* Driver API */
static const struct zmk_input_processor_driver_api inertia_driver_api = {
    .handle_event = inertia_handle_event,
};

#define INERTIA_INST(n)                                                                             \
    static struct inertia_data processor_inertia_data_##n = {};                                     \
    static const struct inertia_config processor_inertia_config_##n = {                             \
        .decay_factor_int = DT_INST_PROP_OR(n, decay_factor_int, DEFAULT_INERTIA_DECAY_FACTOR_INT), \
        .interval_ms = DT_INST_PROP_OR(n, report_interval_ms, DEFAULT_INERTIA_INTERVAL_MS),         \
        .threshold_start = DT_INST_PROP_OR(n, threshold_start, DEFAULT_INERTIA_THRESHOLD_START),    \
        .threshold_stop = DT_INST_PROP_OR(n, threshold_stop, DEFAULT_INERTIA_THRESHOLD_STOP),       \
        .scroll_mode = DT_INST_PROP_OR(n, scroll_mode, false),                                      \
    };                                                                                              \
    DEVICE_DT_INST_DEFINE(n, inertia_init, NULL, &processor_inertia_data_##n,                       \
                          &processor_inertia_config_##n, POST_KERNEL,                               \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &inertia_driver_api);

DT_INST_FOREACH_STATUS_OKAY(INERTIA_INST)