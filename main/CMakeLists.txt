idf_component_register(
    SRCS "src/main.c"
        "src/task/display_task.c"

    INCLUDE_DIRS    "include"
        "include/task"
        "."
    REQUIRES freertos
    REQUIRES dis_driver
)

target_compile_options(${COMPONENT_LIB} PRIVATE "-fexceptions")