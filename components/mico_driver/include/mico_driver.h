// mico_driver.h
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 INMP441 麦克风（I2S 标准模式）
 * @return ESP_OK 成功，其他为错误码
 */
esp_err_t mico_driver_init(void);

/**
 * @brief 读取麦克风 PCM 数据（16-bit 单声道）
 * @param buf        输出缓冲区（int16_t 数组）
 * @param buf_size   缓冲区大小（字节数）
 * @param bytes_read 实际读取的字节数（输出）
 * @return ESP_OK 成功
 */
esp_err_t mico_driver_read(int16_t *buf, size_t buf_size, size_t *bytes_read);

/**
 * @brief 反初始化麦克风驱动，释放资源
 */
void mico_driver_deinit(void);

#ifdef __cplusplus
}
#endif