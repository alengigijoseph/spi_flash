idf_component_register(
    SRCS "battery_fs.c"
    INCLUDE_DIRS "."
    REQUIRES spi_nand_flash driver
)
