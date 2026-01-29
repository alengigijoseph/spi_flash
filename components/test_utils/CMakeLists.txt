idf_component_register(
    SRCS "battery_tests.c"
    INCLUDE_DIRS "include"
    REQUIRES battery_fs
    PRIV_REQUIRES main
)
