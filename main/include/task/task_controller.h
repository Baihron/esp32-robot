#ifndef TASK_CONTROLLER_H
#define TASK_CONTROLLER_H

#include "state_manager.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 初始化任务控制器
void task_controller_init(void);

// 启动任务控制器
void task_controller_start(void);

// 处理状态变化
void task_controller_handle_state_change(system_state_t old_state, system_state_t new_state);

// 获取任务状态
void task_controller_get_status(bool *camera_running, bool *display_running, bool *face_detection_running);

// 强制停止所有任务（紧急情况）
void task_controller_emergency_stop(void);

#ifdef __cplusplus
}
#endif

#endif // TASK_CONTROLLER_H