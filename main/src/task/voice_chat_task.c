#include "voice_chat_task.h"
#include "voice_chat_client.h"
#include "mico_driver.h"
#include "audio_manager.h"
#include "state_manager.h"
#include "task_controller.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "VOICE_CHAT_TASK";

#define PLAYBACK_BUFFER_SIZE    (SAMPLE_RATE * BYTES_PER_SAMPLE * 3)

// 音频录制参数（精确 3 秒）
#define RECORD_DURATION_MS      3000
#define SAMPLE_RATE             16000
#define BITS_PER_SAMPLE         16
#define NUM_CHANNELS            1
#define BYTES_PER_SAMPLE        2

#define RECORD_BUFFER_SIZE      (SAMPLE_RATE * BYTES_PER_SAMPLE * RECORD_DURATION_MS / 1000)  // 96000 字节

// 声音检测参数
#define VAD_CHUNK_SIZE          320     // 20ms
#define VAD_INTERVAL_MS         50
#define VAD_THRESHOLD           500
#define VAD_TRIGGER_COUNT       3
#define VAD_COOLDOWN_MS         1000

typedef struct {
    TaskHandle_t task_handle;
    bool initialized;
    volatile bool recording;
    volatile bool stop_requested;
} voice_chat_task_t;

static voice_chat_task_t g_voice_chat = {0};

static float calculate_rms(const int16_t *buffer, size_t samples)
{
    float sum = 0;
    for (size_t i = 0; i < samples; i++) {
        sum += (float)buffer[i] * buffer[i];
    }
    return sqrtf(sum / samples);
}

// 修正后的录音函数：保证精确读取 96000 字节
static void record_audio(int16_t *buffer, size_t *recorded_bytes)
{
    ESP_LOGI(TAG, "===== Voice detected! Starting 3-second recording =====");
    
    g_voice_chat.recording = true;
    
    size_t total_bytes_needed = RECORD_BUFFER_SIZE;      // 96000 字节
    size_t bytes_read_so_far = 0;
    
    TickType_t end_time = xTaskGetTickCount() + pdMS_TO_TICKS(RECORD_DURATION_MS + 100);
    
    #define READ_CHUNK_BYTES  2048   // 每次读取 2048 字节
    
    while (bytes_read_so_far < total_bytes_needed) {
        if (g_voice_chat.stop_requested) {
            ESP_LOGW(TAG, "Recording stopped by request");
            break;
        }
        if (xTaskGetTickCount() > end_time) {
            ESP_LOGW(TAG, "Recording timeout");
            break;
        }
        
        size_t to_read = READ_CHUNK_BYTES;
        if (bytes_read_so_far + to_read > total_bytes_needed) {
            to_read = total_bytes_needed - bytes_read_so_far;
        }
        
        size_t actual = 0;
        esp_err_t ret = mico_driver_read((int16_t*)((uint8_t*)buffer + bytes_read_so_far), to_read, &actual);
        
        if (ret == ESP_OK && actual > 0) {
            bytes_read_so_far += actual;
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
    
    // 最终实际读到的字节数（理论上等于 total_bytes_needed）
    *recorded_bytes = bytes_read_so_far;
    g_voice_chat.recording = false;
    
    ESP_LOGI(TAG, "===== Recording finished: %zu bytes (expected %zu) =====", 
             *recorded_bytes, total_bytes_needed);

    ESP_LOGI(TAG, "Recorded first 16 samples: %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
         buffer[0], buffer[100], buffer[200], buffer[300], buffer[400], buffer[500], buffer[600], buffer[700],
         buffer[800], buffer[900], buffer[1000], buffer[1100], buffer[1200], buffer[1300], buffer[1400], buffer[1500]);

    size_t total_samples = *recorded_bytes / BYTES_PER_SAMPLE;
    for (size_t i = 0; i < total_samples; i++) {
        int32_t amplified = (int32_t)buffer[i] * 4;   // 放大 4 倍
        if (amplified > 32767) amplified = 32767;
        if (amplified < -32768) amplified = -32768;
        buffer[i] = (int16_t)amplified;
    }
}

// 语音聊天任务主函数
static void voice_chat_task_func(void *pvParameters)
{
    ESP_LOGI(TAG, "Voice chat task started - waiting for voice...");
    
    // 使用 PSRAM 分配录音缓冲区（如果可用）
    int16_t *record_buf = heap_caps_malloc(RECORD_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!record_buf) {
        ESP_LOGE(TAG, "Failed to allocate record buffer (%d bytes)", RECORD_BUFFER_SIZE);
        vTaskDelete(NULL);
        return;
    }

    static int16_t s_reply_buf[PLAYBACK_BUFFER_SIZE / sizeof(int16_t)];

    int16_t vad_buf[VAD_CHUNK_SIZE];
    int trigger_count = 0;
    TickType_t last_record_time = 0;
    
    while (1) {
        if (g_voice_chat.stop_requested) {
            break;
        }
        
        if (g_voice_chat.recording) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        if (xTaskGetTickCount() - last_record_time < pdMS_TO_TICKS(VAD_COOLDOWN_MS)) {
            vTaskDelay(pdMS_TO_TICKS(VAD_INTERVAL_MS));
            continue;
        }
        
        size_t bytes_read = 0;
        esp_err_t ret = mico_driver_read(vad_buf, VAD_CHUNK_SIZE * BYTES_PER_SAMPLE, &bytes_read);
        
        if (ret == ESP_OK && bytes_read > 0) {
            size_t samples = bytes_read / BYTES_PER_SAMPLE;
            float rms = calculate_rms(vad_buf, samples);
            
            if (rms > VAD_THRESHOLD) {
                trigger_count++;
                ESP_LOGD(TAG, "Voice detected! RMS=%.1f (count=%d/%d)", 
                         rms, trigger_count, VAD_TRIGGER_COUNT);
                
                if (trigger_count >= VAD_TRIGGER_COUNT) {
                    size_t recorded_bytes = 0;
                    record_audio(record_buf, &recorded_bytes);
                    last_record_time = xTaskGetTickCount();
                    
                    // 只有录音字节数足够（至少 1024 字节）才发送
                    if (recorded_bytes >= 1024) {
                        ESP_LOGI(TAG, "===== Sending to WebSocket API =====");
                        
                        size_t reply_len = 0;
                        esp_err_t api_ret = voice_chat_client_send_audio(
                            record_buf, recorded_bytes,
                            s_reply_buf, &reply_len,
                            PLAYBACK_BUFFER_SIZE
                        );
                        
                        if (api_ret == ESP_OK && reply_len > 0) {
                            ESP_LOGI(TAG, "===== Received audio reply: %zu bytes =====", reply_len);
                            // 打印前几个值
                            int print_count = (reply_len / sizeof(int16_t) < 16) ? (reply_len / sizeof(int16_t)) : 16;
                            for (int i = 0; i < print_count; i++) {
                                ESP_LOGI(TAG, "  reply[%d] = %d", i, s_reply_buf[i]);
                            }
                        } else {
                            ESP_LOGE(TAG, "Failed to get audio reply: %s", 
                                    api_ret == ESP_OK ? "empty reply" : esp_err_to_name(api_ret));
                        }
                    } else {
                        ESP_LOGW(TAG, "Recorded audio too short: %zu bytes", recorded_bytes);
                    }
                    trigger_count = 0;
                }
            } else {
                if (trigger_count > 0) trigger_count = 0;
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        
        vTaskDelay(pdMS_TO_TICKS(VAD_INTERVAL_MS));
    }
    
    heap_caps_free(record_buf);
    g_voice_chat.task_handle = NULL;
    ESP_LOGI(TAG, "Voice chat task stopped");
    vTaskDelete(NULL);
}

esp_err_t voice_chat_task_init(void)
{
    if (g_voice_chat.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }
    
    esp_err_t ret = voice_chat_client_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init voice chat client");
        return ret;
    }
    
    ret = xTaskCreatePinnedToCore(
        voice_chat_task_func,
        "voice_chat",
        16384,
        NULL,
        tskIDLE_PRIORITY + 1,
        &g_voice_chat.task_handle,
        1
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create voice chat task");
        voice_chat_client_deinit();
        return ESP_FAIL;
    }
    
    g_voice_chat.initialized = true;
    g_voice_chat.recording = false;
    g_voice_chat.stop_requested = false;
    
    ESP_LOGI(TAG, "Voice chat task initialized - listening for voice...");
    return ESP_OK;
}

bool voice_chat_task_is_recording(void)
{
    return g_voice_chat.recording;
}

void voice_chat_task_stop(void)
{
    if (!g_voice_chat.initialized) return;
    g_voice_chat.stop_requested = true;
}

void voice_chat_task_deinit(void)
{
    if (!g_voice_chat.initialized) return;
    voice_chat_task_stop();
    if (g_voice_chat.task_handle) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    voice_chat_client_deinit();
    g_voice_chat.initialized = false;
    ESP_LOGI(TAG, "Voice chat task deinitialized");
}