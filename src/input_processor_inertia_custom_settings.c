/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT zmk_input_processor_inertia
#include <string.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <cormoran/zmk/custom_settings.h>
#include <zmk/event_manager.h>
#include <zmk/studio/custom.h>
#include <zmk-input-inertia/custom_settings.h>
#include <zmk-input-inertia/runtime.h>
LOG_MODULE_REGISTER(inertia_custom_settings, CONFIG_ZMK_INPUT_INERTIA_LOG_LEVEL);
static K_MUTEX_DEFINE(settings_lock);
static bool inertia_namespace_handler(const zmk_custom_CallRequest *request, pb_callback_t *response) {
    ARG_UNUSED(request); ARG_UNUSED(response); return false;
}
static struct zmk_rpc_custom_subsystem_meta metadata = {
    ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS("https://github.com/amgskobo/zmk-input-inertia"),
    .security = ZMK_STUDIO_RPC_HANDLER_UNSECURED,
};
#define REGISTER_SUBSYSTEM(token) ZMK_RPC_CUSTOM_SUBSYSTEM(token, &metadata, inertia_namespace_handler)
REGISTER_SUBSYSTEM(ZMK_INPUT_INERTIA_SUBSYSTEM_TOKEN);

#define INT_SETTING(n, field, key, lo, hi) \
    ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(inertia_cs_##field##_##n, \
        ZMK_INPUT_INERTIA_SUBSYSTEM, INERTIA_SETTING_KEY(n, key), \
        ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, ZMK_CUSTOM_SETTING_VALUE_INT32(DT_INST_PROP(n, field)), \
        ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE, \
        ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_RANGE_INT32(lo, hi));
#define BOOL_SETTING(n, field, key) \
    ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(inertia_cs_##field##_##n, \
        ZMK_INPUT_INERTIA_SUBSYSTEM, INERTIA_SETTING_KEY(n, key), \
        ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL, ZMK_CUSTOM_SETTING_VALUE_BOOL(DT_INST_PROP(n, field)), \
        ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE, \
        ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_NO_CONSTRAINT);
#define SETTINGS(n) INERTIA_ASSERT_KEY(n) INERTIA_INT_FIELDS(INT_SETTING, n) INERTIA_BOOL_FIELDS(BOOL_SETTING, n)
DT_INST_FOREACH_STATUS_OKAY(SETTINGS)

static bool read_int(const struct zmk_custom_setting *setting, int32_t *out) {
    struct zmk_custom_setting_value value;
    if (zmk_custom_setting_read(setting, &value) != 0 || value.type != ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32) { return false; }
    *out = value.int32_value; return true;
}
static bool read_bool(const struct zmk_custom_setting *setting, bool *out) {
    struct zmk_custom_setting_value value;
    if (zmk_custom_setting_read(setting, &value) != 0 || value.type != ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL) { return false; }
    *out = value.bool_value; return true;
}
#define READ_INT(n, field, key, lo, hi) if (!read_int(&inertia_cs_##field##_##n, &params.field)) { break; }
#define READ_BOOL(n, field, key) if (!read_bool(&inertia_cs_##field##_##n, &params.field)) { break; }
#define APPLY(n) do { \
    struct inertia_params params = {0}; \
    INERTIA_INT_FIELDS(READ_INT, n) INERTIA_BOOL_FIELDS(READ_BOOL, n) \
    if (inertia_set_params(DEVICE_DT_INST_GET(n), &params) != 0) { \
        LOG_WRN("Inertia settings snapshot rejected"); \
    } \
} while (0);
static void inertia_apply_settings(void) {
    /* Serialize entire read/apply transactions, including initialization. */
    k_mutex_lock(&settings_lock, K_FOREVER);
    DT_INST_FOREACH_STATUS_OKAY(APPLY)
    k_mutex_unlock(&settings_lock);
}
static int inertia_settings_event(const zmk_event_t *eh) {
    const struct zmk_custom_setting_changed *changed = as_zmk_custom_setting_changed(eh);
    if (changed != NULL && strcmp(changed->setting->custom_subsystem_id, ZMK_INPUT_INERTIA_SUBSYSTEM) != 0) { return ZMK_EV_EVENT_BUBBLE; }
    inertia_apply_settings(); return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(inertia_custom_settings, inertia_settings_event);
ZMK_SUBSCRIPTION(inertia_custom_settings, zmk_custom_setting_changed);
ZMK_SUBSCRIPTION(inertia_custom_settings, zmk_custom_settings_initialized);
static int inertia_check_keys(void) {
    ZMK_CUSTOM_SETTING_FOREACH(setting) {
        if (strcmp(setting->custom_subsystem_id, ZMK_INPUT_INERTIA_SUBSYSTEM) != 0) { continue; }
        ZMK_CUSTOM_SETTING_FOREACH(other) {
            if (other < setting && strcmp(other->custom_subsystem_id, ZMK_INPUT_INERTIA_SUBSYSTEM) == 0 && strcmp(other->key, setting->key) == 0) { LOG_ERR("Duplicate Inertia setting key: %s", setting->key); }
        }
    }
    return 0;
}
SYS_INIT(inertia_check_keys, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
