// mico_driver.c
#include "mico_driver.h"
#include "mico_config.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MICO_DRIVER";
static i2s_chan_handle_t rx_handle = NULL;   // I2S 接收通道句柄

esp_err_t mico_driver_init(void) {
    ESP_LOGI(TAG, "Initializing INMP441 microphone (I2S standard mode)");

    // 1. 配置 I2S 通道（仅接收）
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(MIC_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = MIC_DMA_BUF_COUNT;          // DMA 缓冲区数量
    chan_cfg.dma_frame_num = MIC_DMA_BUF_LEN;               // 每个 DMA 缓冲区的帧数
    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channel: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. 配置 I2S 标准模式参数（专为 INMP441）
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MIC_SAMPLE_RATE),
        .slot_cfg = {
            // INMP441 输出 24-bit 数据，置于 32-bit 槽中
            .data_bit_width = I2S_SLOT_BIT_WIDTH_32BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT,
            .slot_mode = I2S_SLOT_MODE_MONO,            // 单声道
            .slot_mask = I2S_STD_SLOT_LEFT,             // 只接收左声道（L/R 接 GND）
            .ws_width = 32,                             // 字选宽度
            .ws_pol = false,                            // 标准极性
            .bit_shift = true,                          // 位移位
            .left_align = false,                        // 左对齐
            .big_endian = false,                        // 小端
            .bit_order_lsb = false                      // MSB 优先
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,    // INMP441 不需要主时钟
            .bclk = MIC_PIN_BCK,
            .ws   = MIC_PIN_WS,
            .dout = I2S_GPIO_UNUSED,    // 本例仅接收
            .din  = MIC_PIN_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false
            }
        },
    };
    ret = i2s_channel_init_std_mode(rx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2S standard mode: %s", esp_err_to_name(ret));
        i2s_del_channel(rx_handle);
        rx_handle = NULL;
        return ret;
    }

    // 3. 启用 I2S 通道
    ret = i2s_channel_enable(rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S channel: %s", esp_err_to_name(ret));
        i2s_del_channel(rx_handle);
        rx_handle = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "Microphone initialized successfully");
    return ESP_OK;
}

esp_err_t mico_driver_read(int16_t *buf, size_t buf_size, size_t *bytes_read) {
    if (!rx_handle) {
        ESP_LOGE(TAG, "Microphone not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // 临时缓冲区，存储原始 32-bit 数据
    uint32_t raw_samples[buf_size / sizeof(uint32_t)];
    size_t raw_bytes = 0;
    esp_err_t ret = i2s_channel_read(rx_handle, raw_samples, buf_size, &raw_bytes, portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S read failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 转换：32-bit 原始数据 -> 提取 24-bit 有效数据 -> 右移 8 位得到 16-bit
    size_t sample_cnt = raw_bytes / sizeof(uint32_t);
    for (size_t i = 0; i < sample_cnt; i++) {
        // 这里统一右移 16 位转为 16-bit
        buf[i] = (int16_t)(raw_samples[i] >> 16);
    }

    *bytes_read = sample_cnt * sizeof(int16_t);
    return ESP_OK;
}

void mico_driver_deinit(void) {
    if (rx_handle) {
        i2s_channel_disable(rx_handle);
        i2s_del_channel(rx_handle);
        rx_handle = NULL;
        ESP_LOGI(TAG, "Microphone driver deinitialized");
    }
}