/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * An input device that replays a script: each entry waits its own delay after
 * the one before it, then reports one event. Unlike ZMK's input mock, whose
 * period is fixed, a script can pause, hold still and interleave with other
 * devices at chosen moments.
 *
 * One entry type is not an event: (period SCRIPT_GLIDE steps x y) moves a
 * contact from the last reported absolute position to (x, y) in steps frames,
 * one ABS_X/ABS_Y pair every period ms, the way a trackpad reports a stroke.
 */

#define DT_DRV_COMPAT zmk_test_input_script

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(script, CONFIG_ZMK_LOG_LEVEL);

#define SCRIPT_ENTRY_CELLS 5
#define SCRIPT_GLIDE 254

struct script_config {
    const int32_t *entries;
    size_t length;
};

struct script_data {
    const struct device *dev;
    struct k_work_delayable work;
    size_t next;
    /* The last absolute position reported, where a glide starts. */
    int32_t x;
    int32_t y;
    int32_t glide_from_x;
    int32_t glide_from_y;
    int32_t glide_step;
};

static void script_schedule(struct script_data *data, const struct script_config *config) {
    if (data->next >= config->length) {
        LOG_INF("%s: done", data->dev->name);
        return;
    }

    k_work_schedule(&data->work, K_MSEC(config->entries[data->next * SCRIPT_ENTRY_CELLS]));
}

static void script_report(struct script_data *data, uint16_t type, uint16_t code, int32_t value,
                          bool sync) {
    if (type == INPUT_EV_ABS && code == INPUT_ABS_X) {
        data->x = value;
    } else if (type == INPUT_EV_ABS && code == INPUT_ABS_Y) {
        data->y = value;
    }
    input_report(data->dev, type, code, value, sync, K_FOREVER);
}

/* One frame of a glide; true once its last frame has gone out. */
static bool script_glide(struct script_data *data, const int32_t *entry) {
    const int32_t steps = entry[2];

    if (data->glide_step == 0) {
        data->glide_from_x = data->x;
        data->glide_from_y = data->y;
    }
    data->glide_step++;
    script_report(data, INPUT_EV_ABS, INPUT_ABS_X,
                  data->glide_from_x + (entry[3] - data->glide_from_x) * data->glide_step / steps,
                  false);
    script_report(data, INPUT_EV_ABS, INPUT_ABS_Y,
                  data->glide_from_y + (entry[4] - data->glide_from_y) * data->glide_step / steps,
                  true);
    if (data->glide_step < steps) {
        return false;
    }
    data->glide_step = 0;
    return true;
}

static void script_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct script_data *data = CONTAINER_OF(dwork, struct script_data, work);
    const struct script_config *config = data->dev->config;
    const int32_t *entry = &config->entries[data->next * SCRIPT_ENTRY_CELLS];

    if (entry[1] == SCRIPT_GLIDE) {
        if (!script_glide(data, entry)) {
            k_work_schedule(&data->work, K_MSEC(entry[0]));
            return;
        }
        data->next++;
        script_schedule(data, config);
        return;
    }

    /* Entries that follow with no delay go out together, as one frame. */
    do {
        entry = &config->entries[data->next * SCRIPT_ENTRY_CELLS];
        script_report(data, entry[1], entry[2], entry[3], entry[4] != 0);
        data->next++;
    } while (data->next < config->length &&
             config->entries[data->next * SCRIPT_ENTRY_CELLS] == 0);

    script_schedule(data, config);
}

static int script_init(const struct device *dev) {
    struct script_data *data = dev->data;

    data->dev = dev;
    data->next = 0;
    k_work_init_delayable(&data->work, script_work_handler);
    script_schedule(data, dev->config);

    return 0;
}

#define SCRIPT_INST(n)                                                                             \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, events) % SCRIPT_ENTRY_CELLS == 0,                            \
                 "script events are (delay-ms type code value sync) tuples");                      \
    static const int32_t script_entries_##n[] = DT_INST_PROP(n, events);                           \
    static const struct script_config script_config_##n = {                                        \
        .entries = script_entries_##n,                                                             \
        .length = DT_INST_PROP_LEN(n, events) / SCRIPT_ENTRY_CELLS,                                \
    };                                                                                             \
    static struct script_data script_data_##n;                                                     \
    DEVICE_DT_INST_DEFINE(n, script_init, NULL, &script_data_##n, &script_config_##n, POST_KERNEL, \
                          CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(SCRIPT_INST)
