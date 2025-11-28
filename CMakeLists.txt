if (CONFIG_ZMK_INPUT_PROCESSOR_INERTIA)
  zephyr_library()
  zephyr_library_sources(src/input_processor_inertia.c)
endif()
