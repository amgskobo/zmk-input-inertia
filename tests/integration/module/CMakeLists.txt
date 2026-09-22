# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

if(CONFIG_ZMK_INPUT_INERTIA_TEST)
    target_sources(app PRIVATE src/runtime_test.c)
endif()
