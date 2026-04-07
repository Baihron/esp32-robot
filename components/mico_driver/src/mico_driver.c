#include "mico_driver.h"
#include "mico_config.h"
#include "driver/i2s_pdm.h"
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "mic_driver";
static i2s_chan_handle_t rx_handle = NULL;

esp_err_t mic_driver_init(void)
{
    esp_err_t ret;

    /* 分配 I2S RX 通道 */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ret = i2s_new_channel(&chan_cfg, NULL, &rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 初始化为 PDM RX 模式 */
    i2s_pdm_rx_config_t pdm_rx_cfg = {
        .clk_cfg  = I2S_PDM_RX_CLK_DEFAULT_CONFIG(MIC_SAMPLE_RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT,
                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = MIC_PIN_WS,   // PDM 模式下 WS 引脚作为 CLK
            .din = MIC_PIN_SD,
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };

    ret = i2s_channel_init_pdm_rx_mode(rx_handle, &pdm_rx_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_pdm_rx_mode failed: %s", esp_err_to_name(ret));
        i2s_del_channel(rx_handle);
        rx_handle = NULL;
        return ret;
    }

    ret = i2s_channel_enable(rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(ret));
        i2s_del_channel(rx_handle);
        rx_handle = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "Microphone initialized (PDM RX, CLK=GPIO%d, DIN=GPIO%d, rate=%d Hz)",
             MIC_PIN_WS, MIC_PIN_SD, MIC_SAMPLE_RATE);
    return ESP_OK;
}

esp_err_t mic_driver_read(int16_t *buf, size_t buf_size, size_t *bytes_read)
{
    if (rx_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2s_channel_read(rx_handle, buf, buf_size, bytes_read, portMAX_DELAY);
}

void mic_driver_deinit(void)
{
    if (rx_handle != NULL) {
        i2s_channel_disable(rx_handle);
        i2s_del_channel(rx_handle);
        rx_handle = NULL;
        ESP_LOGI(TAG, "Microphone deinitialized");
    }
}