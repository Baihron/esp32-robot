#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 语音聊天任务初始化
 * @return ESP_OK 成功
 */
esp_err_t voice_chat_task_init(void);

/**
 * @brief 开始语音录制（触发一次录制，持续3秒）
 * @return ESP_OK 成功
 */
esp_err_t voice_chat_task_start_recording(void);

/**
 * @brief 检查语音聊天任务是否正在录制
 * @return true 正在录制
 */
bool voice_chat_task_is_recording(void);

/**
 * @brief 检查语音聊天任务是否正在播放回复
 * @return true 正在播放
 */
bool voice_chat_task_is_playing(void);

/**
 * @brief 停止语音聊天任务
 */
void voice_chat_task_stop(void);

/**
 * @brief 反初始化语音聊天任务
 */
void voice_chat_task_deinit(void);

#ifdef __cplusplus
}
#endif