# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

if(CONFIG_ZMK_TEST_INPUT_SCRIPT)
  zephyr_library()
  zephyr_library_sources(src/test_input_script.c)
endif()
