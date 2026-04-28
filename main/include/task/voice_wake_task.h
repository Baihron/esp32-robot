// voice_wake_task.h
#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 唤醒检测回调（检测到唤醒词后调用）
 */
typedef void (*voice_wake_callback_t)(void);

/**
 * @brief 初始化语音唤醒引擎（加载 WakeNet 模型）
 * @param cb 唤醒后的回调（可为 NULL）
 * @return ESP_OK 成功
 */
esp_err_t voice_wake_init(voice_wake_callback_t cb);

/**
 * @brief 启动唤醒检测任务（进入 UNLOCK 模式时调用）
 * @return ESP_OK 成功
 */
esp_err_t voice_wake_start(void);

/**
 * @brief 停止唤醒检测任务（退出 UNLOCK 模式时调用）
 * @return ESP_OK 成功
 */
esp_err_t voice_wake_stop(void);

#ifdef __cplusplus
}
#endif