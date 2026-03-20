#ifndef COMMON_TYPE_H
#define COMMON_TYPE_H

#include "config.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 系统初始化结构体
typedef struct {
    bool camera_enabled;
    bool display_enabled;
    bool sd_card_enabled;
    bool face_detect_enabled;
} system_config_t;

// 任务状态跟踪
typedef struct {
    bool camera_initialized;
    bool display_initialized;
    bool face_detection_initialized;
    bool camera_running;
    bool display_running;
    bool face_detection_running;
} task_status_t;



#ifdef __cplusplus
}
#endif

#endif