#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/**
 * @brief 初始化麦克风（I2S PDM RX 模式）
 * @return ESP_OK 成功，否则返回错误码
 */
esp_err_t mic_driver_init(void);

/**
 * @brief 读取麦克风音频数据
 * @param buf       输出缓冲区（int16_t 数组）
 * @param buf_size  缓冲区字节数
 * @param bytes_read 实际读取的字节数
 * @return ESP_OK 成功
 */
esp_err_t mic_driver_read(int16_t *buf, size_t buf_size, size_t *bytes_read);

/**
 * @brief 反初始化麦克风驱动
 */
void mic_driver_deinit(void);