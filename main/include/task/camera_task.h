#ifndef CAMERA_TASK_H
#define CAMERA_TASK_H

#include "esp_err.h"
#include "cam_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "common_type.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 摄像头捕获任务配置结构体
 */
typedef struct {
    TaskHandle_t task_handle;          ///< 任务句柄
    uint32_t capture_interval_ms;      ///< 捕获间隔（毫秒）
    uint32_t frame_count;              ///< 捕获的帧数统计
    uint32_t send_fail_count;          ///< 发送失败计数
} camera_task_config_t;

/**
 * @brief 初始化摄像头捕获任务
 * 
 * @param capture_interval_ms 捕获间隔（毫秒）
 * @param priority 任务优先级
 * @param stack_size 任务堆栈大小
 * @param core_id 运行的核心ID
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t camera_capture_task_init(uint32_t capture_interval_ms,
                                   UBaseType_t priority,
                                   uint32_t stack_size,
                                   BaseType_t core_id);

/**
 * @brief 启动摄像头捕获任务
 * 
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t camera_capture_task_start(void);

/**
 * @brief 停止摄像头捕获任务
 * 
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t camera_capture_task_stop(void);

/**
 * @brief 获取摄像头捕获任务句柄
 * 
 * @return TaskHandle_t 任务句柄
 */
TaskHandle_t camera_capture_task_get_handle(void);

/**
 * @brief 检查摄像头捕获任务是否正在运行
 * 
 * @return true 正在运行
 * @return false 未运行
 */
bool camera_capture_task_is_running(void);

/**
 * @brief 获取摄像头捕获统计信息
 * 
 * @param total_frames 总帧数（输出）
 * @param failed_frames 失败帧数（输出）
 */
void camera_capture_task_get_stats(uint32_t *total_frames, uint32_t *failed_frames);

/**
 * @brief 设置捕获间隔
 * 
 * @param interval_ms 捕获间隔（毫秒）
 */
void camera_capture_task_set_interval(uint32_t interval_ms);

/**
 * @brief 反初始化摄像头捕获任务
 */
void camera_capture_task_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* CAMERA_TASK_H */