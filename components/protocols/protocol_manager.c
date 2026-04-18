// components/protocols/protocol_manager.c
#include "protocol_manager.h"
#include "audio_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "PROTOCOL_MGR";
static TaskHandle_t s_protocol_task_handle = NULL;
static bool s_is_running = false;

static void protocol_task(void *arg) {
    ESP_LOGI(TAG, "Protocol task started");
    while (1) {
        // 1. 发送音频数据到服务器
        void* packet = audio_manager_pop_send_packet();
        if (packet) {
            // TODO: 将packet转换为实际类型，并通过MQTT/WebSocket发送
            // ... 网络发送代码 ...
            // delete packet; // 发送后释放内存
        }
        
        // 2. 接收来自服务器的音频数据
        // TODO: 从网络接收数据到 recv_data, recv_size 等变量
        // if (received) {
        //     audio_manager_push_decode_packet(recv_data, recv_size, sample_rate, frame_duration, timestamp);
        // }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelete(NULL);
}

esp_err_t protocol_manager_start(void) {
    if (s_is_running) {
        ESP_LOGW(TAG, "Protocol already running");
        return ESP_OK;
    }
    xTaskCreatePinnedToCore(protocol_task, "protocol_task", 8192, NULL, 5, &s_protocol_task_handle, 0);
    s_is_running = true;
    ESP_LOGI(TAG, "Protocol manager started");
    return ESP_OK;
}