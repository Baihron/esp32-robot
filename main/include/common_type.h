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
    bool face_recognition_initialized;
    bool camera_running;
    bool display_running;
    bool face_detection_running;
    bool face_recognition_running;
} task_status_t;

// 系统状态定义
typedef enum {
    STATE_SLEEP = 0,        // 休眠状态，所有任务关闭
    STATE_LOCKED,           // 锁定状态，需要人脸识别解锁
    STATE_UNLOCKED,         // 解锁状态，正常使用
    STATE_FACE_ENROLLING,   // 人脸录入状态
    STATE_SHUTTING_DOWN     // 关机中
} system_state_t;

// 系统事件定义
typedef enum {
    EVENT_POWER_ON = 0,     // 开机（按钮长按）
    EVENT_POWER_OFF,        // 关机（按钮长按）
    EVENT_UNLOCK_SUCCESS,   // 解锁成功
    EVENT_START_ENROLL,     // 开始人脸录入（按钮双击）
    EVENT_ENROLL_COMPLETE,  // 人脸录入完成
    EVENT_ENROLL_CANCEL     // 人脸录入取消
} system_event_t;

#ifdef __cplusplus
}
#endif

#endif