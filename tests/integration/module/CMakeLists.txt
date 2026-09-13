if(CONFIG_ZMK_INPUT_INERTIA_TEST)
    target_sources(app PRIVATE src/runtime_test.c)
endif()
