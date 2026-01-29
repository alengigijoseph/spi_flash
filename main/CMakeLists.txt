idf_component_register(
    SRCS "DataAcquisition.c" "main.c"
    INCLUDE_DIRS "." "include"
    REQUIRES battery_fs BATMON test_utils
)