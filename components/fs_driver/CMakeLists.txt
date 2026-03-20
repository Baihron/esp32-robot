idf_component_register(
    SRCS "src/fs_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES fatfs
    REQUIRES sdmmc
)