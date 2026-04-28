idf_component_register(
    SRCS "src/main.c"
        "src/task/camera_task.c"
        "src/task/display_task.c"
        "src/task/button_task.c"
        "src/task/face_detect_task.cpp"
        "src/task/face_recognition_task.cpp"
        "src/task/state_manager.c"
        "src/task/task_controller.c"
        "src/task/voice_chat_task.c"

        "src/utils/frame_queue.c"
        "src/utils/system_init.c"
        "src/utils/wifi_sta.c"
        "src/utils/voice_chat_client.c"

        "src/emotion/emotion_system.c"
        "src/emotion/eye_tracking.c"

    INCLUDE_DIRS    "include"
        "include/emotion"
        "include/task"
        "include/utils"
        "."
    REQUIRES freertos
    REQUIRES dis_driver
    REQUIRES cam_driver
    REQUIRES button_driver
    REQUIRES fs_driver
    REQUIRES flash_driver
    REQUIRES mico_driver
    REQUIRES spiffs
    REQUIRES esp_websocket_client
    REQUIRES esp_http_client
    REQUIRES mbedtls
)

target_compile_options(${COMPONENT_LIB} PRIVATE "-fexceptions")