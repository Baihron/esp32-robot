#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "audio_service.h"
#include "my_audio_codec.h"
#include "websocket_protocol.h"

static const char* TAG = "AUDIO_INIT";

void audio_task(void* pvParameters) {
    ESP_LOGI(TAG, "Starting audio service");

    // 1. 创建编解码器实例
    MyAudioCodec codec;
    codec.Init();

    // 2. 创建 WebSocket 协议（连接 xiaozhi.me 服务器）
    //    需要替换为你的设备 ID 和 API Key（可在 xiaozhi.me 注册获取）
    WebsocketProtocol protocol("wss://xiaozhi.me", "your_device_id", "your_api_key");

    // 3. 创建音频服务并启动
    AudioService service(&codec, &protocol);
    service.Start();  // 阻塞运行

    vTaskDelete(NULL);
}

void start_audio_service(void) {
    xTaskCreate(audio_task, "audio_task", 8192, NULL, 5, NULL);
}