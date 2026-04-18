# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(PARTITION_TABLE_CSV ${CMAKE_SOURCE_DIR}/partitions.csv)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS components)

idf_component_get_property(lcd_lib esp_lcd COMPONENT_LIB)
if(lcd_lib)
    set_source_files_properties(
        "${IDF_PATH}/components/esp_lcd/rgb/esp_lcd_panel_rgb.c"
        PROPERTIES COMPILE_OPTIONS "-O0"
    )
endif()

project(esp32_demo)
