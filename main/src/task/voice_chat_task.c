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

// 音频录制参数
#define RECORD_DURATION_MS      3000    // 录制时长3秒
#define SAMPLE_RATE             16000   // 采样率16kHz
#define BITS_PER_SAMPLE         16      // 16-bit
#define NUM_CHANNELS            1       // 单声道
#define BYTES_PER_SAMPLE        2       // 16-bit = 2 bytes

#define RECORD_BUFFER_SIZE      (SAMPLE_RATE * BYTES_PER_SAMPLE * RECORD_DURATION_MS / 1000)  // 3秒的PCM数据

// 声音检测参数
#define VAD_CHUNK_SIZE          320     // 每次检测20ms的音频 (16kHz * 0.02s = 320 samples)
#define VAD_INTERVAL_MS         50      // 每50ms检测一次
#define VAD_THRESHOLD           500     // 音量阈值（根据实际环境调整）
#define VAD_TRIGGER_COUNT       3       // 连续3次超过阈值才触发（防误触）
#define VAD_COOLDOWN_MS         1000    // 录音完成后冷却1秒，防止重复触发

// 任务控制结构
typedef struct {
    TaskHandle_t task_handle;
    bool initialized;
    volatile bool recording;
    volatile bool stop_requested;
} voice_chat_task_t;

static voice_chat_task_t g_voice_chat = {0};

// 计算音频能量（RMS）
static float calculate_rms(const int16_t *buffer, size_t samples)
{
    float sum = 0;
    for (size_t i = 0; i < samples; i++) {
        sum += (float)buffer[i] * buffer[i];
    }
    return sqrtf(sum / samples);
}

// 录制音频数据
static void record_audio(int16_t *buffer, size_t *recorded_bytes)
{
    ESP_LOGI(TAG, "===== Voice detected! Starting 3-second recording =====");
    
    g_voice_chat.recording = true;
    
    size_t total_samples = (SAMPLE_RATE * RECORD_DURATION_MS) / 1000;
    size_t samples_read = 0;
    
    TickType_t end_time = xTaskGetTickCount() + pdMS_TO_TICKS(RECORD_DURATION_MS + 100);
    
    #define READ_CHUNK_SIZE  512
    
    while (samples_read < total_samples) {
        if (g_voice_chat.stop_requested) {
            ESP_LOGW(TAG, "Recording stopped by request");
            break;
        }
        
        if (xTaskGetTickCount() > end_time) {
            break;
        }
        
        size_t remaining = total_samples - samples_read;
        size_t to_read = (remaining < READ_CHUNK_SIZE) ? remaining : READ_CHUNK_SIZE;
        
        size_t bytes_read = 0;
        esp_err_t ret = mico_driver_read(buffer + samples_read, 
                                          to_read * BYTES_PER_SAMPLE, 
                                          &bytes_read);
        
        if (ret == ESP_OK && bytes_read > 0) {
            samples_read += bytes_read / BYTES_PER_SAMPLE;
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
    
    *recorded_bytes = samples_read * BYTES_PER_SAMPLE;
    g_voice_chat.recording = false;
    
    ESP_LOGI(TAG, "===== Recording finished: %zu bytes (%zu samples) =====", 
             *recorded_bytes, samples_read);
}

// 语音聊天任务主函数
static void voice_chat_task_func(void *pvParameters)
{
    ESP_LOGI(TAG, "Voice chat task started - waiting for voice...");
    
    // 分配录音缓冲区
    int16_t *record_buf = heap_caps_malloc(RECORD_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!record_buf) {
        ESP_LOGE(TAG, "Failed to allocate record buffer (%d bytes)", RECORD_BUFFER_SIZE);
        vTaskDelete(NULL);
        return;
    }
    
    // 声音检测变量
    int16_t vad_buf[VAD_CHUNK_SIZE];
    int trigger_count = 0;
    TickType_t last_record_time = 0;
    
    while (1) {
        if (g_voice_chat.stop_requested) {
            break;
        }
        
        // 如果正在录音，等待录音完成
        if (g_voice_chat.recording) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        // 冷却时间检查（录音完成后1秒内不检测）
        if (xTaskGetTickCount() - last_record_time < pdMS_TO_TICKS(VAD_COOLDOWN_MS)) {
            vTaskDelay(pdMS_TO_TICKS(VAD_INTERVAL_MS));
            continue;
        }
        
        // 读取一小段音频用于声音检测
        size_t bytes_read = 0;
        esp_err_t ret = mico_driver_read(vad_buf, VAD_CHUNK_SIZE * BYTES_PER_SAMPLE, &bytes_read);
        
        if (ret == ESP_OK && bytes_read > 0) {
            size_t samples = bytes_read / BYTES_PER_SAMPLE;
            
            // 计算音量
            float rms = calculate_rms(vad_buf, samples);
            
            // 音量大于阈值
            if (rms > VAD_THRESHOLD) {
                trigger_count++;
                ESP_LOGD(TAG, "Voice detected! RMS=%.1f (count=%d/%d)", 
                         rms, trigger_count, VAD_TRIGGER_COUNT);
                
                // 连续多次检测到声音，触发录音
                if (trigger_count >= VAD_TRIGGER_COUNT) {
                    // 开始录音
                    size_t recorded_bytes = 0;
                    record_audio(record_buf, &recorded_bytes);
                    last_record_time = xTaskGetTickCount();
                    
                    if (recorded_bytes >= 1024) {
                        // 调用API - 端到端语音
                        ESP_LOGI(TAG, "===== Sending to WebSocket API =====");
                        
                        // int16_t *reply_buf = heap_caps_malloc(PLAYBACK_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                        int16_t *reply_buf = malloc(PLAYBACK_BUFFER_SIZE);
                        if (reply_buf) {
                            size_t reply_len = 0;
                            esp_err_t api_ret = voice_chat_client_send_audio(
                                record_buf, recorded_bytes,
                                reply_buf, &reply_len,
                                PLAYBACK_BUFFER_SIZE
                            );
                            
                            if (api_ret == ESP_OK && reply_len > 0) {
                                ESP_LOGI(TAG, "===== Received audio reply: %zu bytes =====", reply_len);
                                
                                // 打印前16个采样值
                                int print_count = (reply_len / sizeof(int16_t) < 16) ? 
                                                  (reply_len / sizeof(int16_t)) : 16;
                                for (int i = 0; i < print_count; i++) {
                                    ESP_LOGI(TAG, "  reply[%d] = %d", i, reply_buf[i]);
                                }
                            } else {
                                ESP_LOGE(TAG, "Failed to get audio reply: %s", 
                                         api_ret == ESP_OK ? "empty reply" : esp_err_to_name(api_ret));
                            }
                            
                            // heap_caps_free(reply_buf);
                            free(reply_buf);
                        } else {
                            ESP_LOGE(TAG, "Failed to allocate reply buffer (%d bytes)", PLAYBACK_BUFFER_SIZE);
                        }
                    } else {
                        ESP_LOGW(TAG, "Recorded audio too short: %zu bytes", recorded_bytes);
                    }
                    
                    trigger_count = 0;  // 重置计数
                }
            } else {
                // 音量低于阈值，重置计数
                if (trigger_count > 0) {
                    trigger_count = 0;
                }
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
    
    // 初始化语音聊天客户端
    esp_err_t ret = voice_chat_client_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init voice chat client");
        return ret;
    }
    
    // 创建任务
    ret = xTaskCreatePinnedToCore(
        voice_chat_task_func,
        "voice_chat",
        8192,
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
    if (!g_voice_chat.initialized) {
        return;
    }
    g_voice_chat.stop_requested = true;
}

void voice_chat_task_deinit(void)
{
    if (!g_voice_chat.initialized) {
        return;
    }
    
    voice_chat_task_stop();
    
    if (g_voice_chat.task_handle) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    voice_chat_client_deinit();
    g_voice_chat.initialized = false;
    ESP_LOGI(TAG, "Voice chat task deinitialized");
}