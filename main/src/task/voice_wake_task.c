// voice_wake_task.c
#include "voice_wake_task.h"
#include "esp_log.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "model_path.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>

static const char *TAG = "VOICE_WAKE";

static esp_afe_sr_iface_t   *afe_handle    = NULL;
static esp_afe_sr_data_t    *afe_data      = NULL;
static voice_wake_callback_t s_user_cb     = NULL;
static TaskHandle_t          s_wake_task_handle = NULL;
static TaskHandle_t          s_feed_task_handle = NULL;
static volatile bool         s_task_running = false;

/* 唤醒检测任务 */
static void wake_task(void *arg)
{
    while (s_task_running) {
        afe_fetch_result_t *result = afe_handle->fetch(afe_data);
        if (result == NULL) {
            continue;
        }

        if (result->wakeup_state == WAKENET_DETECTED) {
            ESP_LOGI(TAG, ">>> Wake word detected! <<<");
            if (s_user_cb) {
                s_user_cb();
            }
            // 防止短时间重复触发
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }

    vTaskDelete(NULL);
}

/* 音频输入喂数据任务 */
static void feed_task(void *arg)
{
    int feed_chunksize = afe_handle->get_feed_chunksize(afe_data);
    int feed_nch = afe_handle->get_feed_channel_num(afe_data);
    int16_t *feed_buff = (int16_t *) malloc(feed_chunksize * feed_nch * sizeof(int16_t));
    if (feed_buff == NULL) {
        ESP_LOGE(TAG, "Failed to allocate feed buffer");
        vTaskDelete(NULL);
        return;
    }

    while (s_task_running) {
        // TODO: 从您的麦克风驱动读取音频数据到 feed_buff
        // 数据格式：16-bit signed，16kHz，通道交错排列
        // 例如：i2s_read(..., feed_buff, feed_chunksize * feed_nch * sizeof(int16_t), ...);

        afe_handle->feed(afe_data, feed_buff);
    }

    free(feed_buff);
    vTaskDelete(NULL);
}

esp_err_t voice_wake_init(voice_wake_callback_t cb)
{
    s_user_cb = cb;

    // 步骤1：初始化模型列表（从 flash 的 "model" 分区加载）
    srmodel_list_t *models = esp_srmodel_init("model");
    if (models == NULL) {
        ESP_LOGE(TAG, "Failed to init sr models");
        return ESP_FAIL;
    }

    // 步骤2：初始化 AFE 配置
    // 单麦克风无参考通道用 "M"，有参考通道用 "MR"，请根据实际硬件调整
    afe_config_t *afe_config = afe_config_init("MR", models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    if (afe_config == NULL) {
        ESP_LOGE(TAG, "Failed to init AFE config");
        return ESP_FAIL;
    }

    // 选择唤醒词模型（选第一个可用的 WakeNet 模型）
    afe_config->wakenet_model_name = esp_srmodel_filter(models, ESP_WN_PREFIX, NULL);
    if (afe_config->wakenet_model_name == NULL) {
        ESP_LOGE(TAG, "No WakeNet model found, check menuconfig");
        return ESP_FAIL;
    }

    // 禁用 AEC（无参考音频时可关闭）
    afe_config->aec_init = false;

    // 步骤3：通过配置获取 AFE 句柄（V2.x 替代 ESP_AFE_SR_HANDLE）
    afe_handle = esp_afe_handle_from_config(afe_config);
    if (afe_handle == NULL) {
        ESP_LOGE(TAG, "Failed to get AFE handle");
        return ESP_FAIL;
    }

    // 步骤4：创建 AFE 实例
    afe_data = afe_handle->create_from_config(afe_config);
    if (afe_data == NULL) {
        ESP_LOGE(TAG, "Failed to create AFE instance");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Voice wake initialized. Model: %s", afe_config->wakenet_model_name);
    return ESP_OK;
}

esp_err_t voice_wake_start(void)
{
    if (s_task_running) {
        return ESP_OK;
    }
    if (afe_handle == NULL || afe_data == NULL) {
        ESP_LOGE(TAG, "Wake engine not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    s_task_running = true;

    // 创建喂数据任务（绑定到 Core 0）
    BaseType_t ret = xTaskCreatePinnedToCore(
        feed_task, "feed_task", 4096, NULL, 5, &s_feed_task_handle, 0
    );
    if (ret != pdPASS) {
        s_task_running = false;
        ESP_LOGE(TAG, "Failed to create feed task");
        return ESP_FAIL;
    }

    // 创建唤醒检测任务（绑定到 Core 1）
    ret = xTaskCreatePinnedToCore(
        wake_task, "voice_wake", 4096, NULL, 5, &s_wake_task_handle, 1
    );
    if (ret != pdPASS) {
        s_task_running = false;
        ESP_LOGE(TAG, "Failed to create voice wake task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Voice wake task started");
    return ESP_OK;
}

esp_err_t voice_wake_stop(void)
{
    s_task_running = false;
    vTaskDelay(pdMS_TO_TICKS(200));

    if (s_wake_task_handle != NULL) {
        vTaskDelete(s_wake_task_handle);
        s_wake_task_handle = NULL;
    }
    if (s_feed_task_handle != NULL) {
        vTaskDelete(s_feed_task_handle);
        s_feed_task_handle = NULL;
    }

    ESP_LOGI(TAG, "Voice wake task stopped");
    return ESP_OK;
}