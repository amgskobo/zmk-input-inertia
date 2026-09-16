zephyr_include_directories(include)

if(CONFIG_ZMK_INPUT_PROCESSOR_INERTIA)
  zephyr_library()

  # ZMK's input-processor API is an application header, not a Zephyr header.
  zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)
  zephyr_library_sources(
    src/inertia_core.c
    src/input_processor_inertia.c
  )
endif()
