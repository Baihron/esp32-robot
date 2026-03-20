#ifndef DISPLAY_TASK_H
#define DISPLAY_TASK_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "dis_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "common_type.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 显示任务初始化
 * 
 * @param priority 任务优先级
 * @param stack_size 任务堆栈大小（字节）
 * @param core_id 运行的核心ID
 * @return esp_err_t 错误码
 */
esp_err_t display_task_init(UBaseType_t priority,
                           uint32_t stack_size,
                           BaseType_t core_id);

/**
 * @brief 启动显示任务
 * 
 * @return esp_err_t 错误码
 */
esp_err_t display_task_start(void);

/**
 * @brief 停止显示任务
 * 
 * @return esp_err_t 错误码
 */
esp_err_t display_task_stop(void);

/**
 * @brief 获取显示任务句柄
 * 
 * @return TaskHandle_t 任务句柄
 */
TaskHandle_t display_task_get_handle(void);

/**
 * @brief 检查显示任务是否正在运行
 * 
 * @return true 正在运行
 * @return false 未运行
 */
bool display_task_is_running(void);

/**
 * @brief 反初始化显示任务
 */
void display_task_deinit(void);

/**
 * @brief 获取帧缓冲区指针
 * 
 * @return uint16_t* 帧缓冲区指针
 */
uint16_t *display_task_get_framebuffer(void);

/**
 * @brief 获取LCD屏幕尺寸
 * 
 * @param width 宽度输出参数
 * @param height 高度输出参数
 */
void display_task_get_lcd_size(uint16_t *width, uint16_t *height);

/**
 * @brief 获取显示任务统计信息
 * 
 * @param displayed 已显示的帧数
 * @param process_time 上次处理时间（毫秒）
 * @param error_count 错误计数
 */
void display_task_get_stats(uint32_t *displayed, 
                           uint32_t *process_time,
                           uint32_t *error_count);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_TASK_H */