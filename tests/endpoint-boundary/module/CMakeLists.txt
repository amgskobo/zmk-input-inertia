# SPDX-License-Identifier: MIT
target_sources(app PRIVATE src/boundary.c)
zephyr_link_libraries(-Wl,--wrap=zmk_endpoint_send_mouse_report)
