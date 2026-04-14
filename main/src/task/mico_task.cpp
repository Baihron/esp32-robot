// mico_task.cpp
#include "task/mico_task.h"
#include "mico_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MICO_TASK";

static TaskHandle_t s_mic_validation_task_handle = nullptr;

// 音频缓冲区（16-bit 单声道，512 个采样点 ≈ 32ms @16kHz）
#define AUDIO_BUF_SAMPLES  512
static int16_t audio_buffer[AUDIO_BUF_SAMPLES];

// 简单统计：每 1 秒打印一次平均幅度和最大幅度
static void mic_validation_task(void *pvParameters) {
    ESP_LOGI(TAG, "Mic validation task started");

    // 初始化麦克风驱动
    esp_err_t ret = mico_driver_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init microphone driver: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    size_t bytes_read;
    uint32_t read_count = 0;
    int32_t sum_abs = 0;
    int16_t max_sample = 0;

    while (1) {
        // 读取一帧音频数据
        ret = mico_driver_read(audio_buffer, sizeof(audio_buffer), &bytes_read);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Read error: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        size_t samples = bytes_read / sizeof(int16_t);
        if (samples == 0) {
            continue;
        }

        // 统计这一帧的最大值和绝对值之和
        int16_t frame_max = 0;
        int32_t frame_sum_abs = 0;
        for (size_t i = 0; i < samples; i++) {
            int16_t abs_val = abs(audio_buffer[i]);
            if (abs_val > frame_max) frame_max = abs_val;
            frame_sum_abs += abs_val;
        }

        // 累加到全局统计（用于每秒打印）
        sum_abs += frame_sum_abs;
        if (frame_max > max_sample) max_sample = frame_max;
        read_count++;

        // 每读取 30 帧（约 30 * 32ms ≈ 1 秒）打印一次结果
        if (read_count >= 30) {
            int32_t avg_abs = sum_abs / (read_count * samples);  // 平均绝对值
            ESP_LOGI(TAG, "Mic stats: avg_abs=%d, max_sample=%d", avg_abs, max_sample);

            // 如果最大幅度大于 500，说明有较大声音输入（可根据实际情况调整阈值）
            if (max_sample > 500) {
                ESP_LOGI(TAG, ">>> Loud sound detected! <<<");
            } else if (max_sample < 50) {
                ESP_LOGW(TAG, "Low volume, check mic connection or speak louder");
            }

            // 重置统计
            sum_abs = 0;
            max_sample = 0;
            read_count = 0;
        }

        // 小延时让出 CPU（实际读取已经阻塞，但为了响应任务删除等，加一个短延时）
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void start_mico_task(void) {
    BaseType_t ret = xTaskCreatePinnedToCore(
        mic_validation_task,
        "mic_validation_task",
        4096,
        NULL,
        tskIDLE_PRIORITY + 4,
        &s_mic_validation_task_handle,
        1
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create face recognition task");
        return;
    }
}