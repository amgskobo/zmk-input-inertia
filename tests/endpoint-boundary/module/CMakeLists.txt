# SPDX-License-Identifier: MIT
target_sources(app PRIVATE src/boundary.c)
zephyr_link_libraries(-Wl,--wrap=zmk_endpoint_send_mouse_report)
zephyr_link_libraries(-Wl,--wrap=zmk_endpoint_get_selected)
zephyr_link_libraries(-Wl,--wrap=zmk_hid_mouse_movement_set)
zephyr_link_libraries(-Wl,--wrap=zmk_hid_mouse_scroll_set)
