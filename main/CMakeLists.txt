idf_component_register(
    SRCS "src/main.c"
        "src/task/camera_task.c"
        "src/task/display_task.c"
        "src/task/button_task.c"
        "src/task/face_detect_task.cpp"
        "src/task/face_recognition_task.cpp"
        "src/task/state_manager.c"
        "src/task/task_controller.c"

        "src/utils/frame_queue.c"
        "src/utils/system_init.c"

    INCLUDE_DIRS    "include"
        "include/task"
        "include/utils"
        "."
    REQUIRES freertos
    REQUIRES dis_driver
    REQUIRES cam_driver
    REQUIRES button_driver
    REQUIRES fs_driver
    REQUIRES flash_driver
    REQUIRES spiffs
)

target_compile_options(${COMPONENT_LIB} PRIVATE "-fexceptions")