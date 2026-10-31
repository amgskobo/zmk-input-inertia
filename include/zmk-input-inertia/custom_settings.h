/* SPDX-License-Identifier: MIT */
#pragma once
#include <zephyr/devicetree.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>
/* Readable group ID; cancel_on_ctrl permits a 13-character node at 64 bytes. */
#define ZMK_INPUT_INERTIA_SUBSYSTEM_TOKEN amgskobo__inertia
#define ZMK_INPUT_INERTIA_SUBSYSTEM STRINGIFY(ZMK_INPUT_INERTIA_SUBSYSTEM_TOKEN)
#define INERTIA_SETTING_KEY(n, field) DT_NODE_FULL_NAME(DT_DRV_INST(n)) "." field
#define INERTIA_STORAGE_NAME(n, field) \
    "custom_settings/" ZMK_INPUT_INERTIA_SUBSYSTEM "/" INERTIA_SETTING_KEY(n, field)
#define INERTIA_ASSERT_KEY(n) \
    BUILD_ASSERT(sizeof(INERTIA_SETTING_KEY(n, "cancel_on_ctrl")) <= CONFIG_ZMK_CUSTOM_SETTINGS_KEY_MAX_LEN, "Inertia setting key too long"); \
    BUILD_ASSERT(sizeof(INERTIA_STORAGE_NAME(n, "cancel_on_ctrl")) <= SETTINGS_MAX_NAME_LEN, "Inertia storage key too long");
