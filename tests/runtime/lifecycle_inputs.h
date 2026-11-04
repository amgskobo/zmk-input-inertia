/* SPDX-License-Identifier: MIT */
#pragma once

/* Controlled ZMK endpoint/event inputs; production driver/state layouts are
 * still extracted unchanged from the owning source by the runtime entry. */
enum zmk_transport { ZMK_TRANSPORT_NONE, ZMK_TRANSPORT_USB, ZMK_TRANSPORT_BLE };
struct zmk_endpoint_instance {
    enum zmk_transport transport;
    struct {
        int profile_index;
    } ble;
};
enum zmk_activity_state { ZMK_ACTIVITY_ACTIVE, ZMK_ACTIVITY_IDLE, ZMK_ACTIVITY_SLEEP };
struct zmk_endpoint_changed {
    struct zmk_endpoint_instance endpoint;
};
struct zmk_activity_state_changed {
    enum zmk_activity_state state;
};
typedef struct {
    enum { TEST_EVENT_OTHER, TEST_EVENT_ENDPOINT, TEST_EVENT_ACTIVITY } type;
    struct zmk_endpoint_changed endpoint;
    struct zmk_activity_state_changed activity;
} zmk_event_t;
#define ZMK_EV_EVENT_BUBBLE 0
