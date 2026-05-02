#include "voice_chat_task.h"
#include "voice_chat_client.h"
#include "llm_chat_client.h"
#include "mico_driver.h"
#include "state_manager.h"
#include "task_controller.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "VOICE_CHAT_TASK";

// 音频录制参数
#define RECORD_DURATION_MS      3000
#define SAMPLE_RATE             16000
#define BITS_PER_SAMPLE         16
#define NUM_CHANNELS            1
#define BYTES_PER_SAMPLE        2

#define RECORD_BUFFER_SIZE      (SAMPLE_RATE * BYTES_PER_SAMPLE * RECORD_DURATION_MS / 1000)

// 声音检测参数
#define VAD_CHUNK_SIZE          320
#define VAD_INTERVAL_MS         50
#define VAD_THRESHOLD           300
#define VAD_TRIGGER_COUNT       3
#define VAD_COOLDOWN_MS         1000

#define ASR_RESULT_MAX_LEN      512    // 识别结果最大长度

typedef struct {
    TaskHandle_t task_handle;
    bool initialized;
    volatile bool recording;
    volatile bool stop_requested;
} voice_chat_task_t;

static voice_chat_task_t g_voice_chat = {0};

extern task_status_t g_tasks;

static float calculate_rms(const int16_t *buffer, size_t samples)
{
    float sum = 0;
    for (size_t i = 0; i < samples; i++) {
        sum += (float)buffer[i] * buffer[i];
    }
    return sqrtf(sum / samples);
}

// 录音函数（保持不变）
static void record_audio(int16_t *buffer, size_t *recorded_bytes)
{
    ESP_LOGI(TAG, "===== Voice detected! Starting 3-second recording =====");
    
    g_voice_chat.recording = true;
    
    size_t total_bytes_needed = RECORD_BUFFER_SIZE;
    size_t bytes_read_so_far = 0;
    
    TickType_t end_time = xTaskGetTickCount() + pdMS_TO_TICKS(RECORD_DURATION_MS + 100);
    
    #define READ_CHUNK_BYTES  2048
    
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
        esp_err_t ret = mico_driver_read(
            (int16_t*)((uint8_t*)buffer + bytes_read_so_far), to_read, &actual);
        
        if (ret == ESP_OK && actual > 0) {
            bytes_read_so_far += actual;
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
    
    *recorded_bytes = bytes_read_so_far;
    g_voice_chat.recording = false;
    
    ESP_LOGI(TAG, "===== Recording finished: %zu bytes (expected %zu) =====", 
             *recorded_bytes, total_bytes_needed);
}

// 语音识别任务主函数
static void voice_chat_task_func(void *pvParameters)
{
    ESP_LOGI(TAG, "Voice ASR task started - waiting for voice...");
    
    int16_t *record_buf = heap_caps_malloc(RECORD_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!record_buf) {
        ESP_LOGE(TAG, "Failed to allocate record buffer (%d bytes)", RECORD_BUFFER_SIZE);
        vTaskDelete(NULL);
        return;
    }

    char asr_result[ASR_RESULT_MAX_LEN];

    int16_t vad_buf[VAD_CHUNK_SIZE];
    int trigger_count = 0;
    TickType_t last_record_time = 0;

    while (1) {
        if(!g_tasks.voice_running) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (g_voice_chat.stop_requested) {
            break;
        }

        if (g_voice_chat.recording) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // 冷却时间
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
                    
                    // 发送到语音识别 API
                    if (recorded_bytes >= 1024) {
                        ESP_LOGI(TAG, "===== Sending to ASR API =====");
                        
                        memset(asr_result, 0, sizeof(asr_result));
                        esp_err_t api_ret = voice_chat_client_send_audio(
                            record_buf, recorded_bytes,
                            asr_result, ASR_RESULT_MAX_LEN
                        );
                        
                        if (api_ret == ESP_OK && strlen(asr_result) > 0) {
                            ESP_LOGI(TAG, "Text: \"%s\"", asr_result);

                            // 调用大模型获取情感
                            char emotion[32] = {0};
                            esp_err_t llm_ret = llm_chat_get_emotion(asr_result, emotion, sizeof(emotion));
                            if (llm_ret == ESP_OK && strlen(emotion) > 0) {
                                ESP_LOGI(TAG, "Detected emotion: %s", emotion);
                                // 在真实项目中替换为你的表情控制函数，例如 set_emotion(emotion);
                            } else {
                                ESP_LOGE(TAG, "Failed to get emotion from LLM");
                                // 可选：设置默认表情
                            }
                        } else {
                            ESP_LOGE(TAG, "Recognition failed: %s", 
                                    api_ret == ESP_OK ? "empty result" : esp_err_to_name(api_ret));
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
    ESP_LOGI(TAG, "Voice ASR task stopped");
    vTaskDelete(NULL);
}

esp_err_t voice_chat_task_init(void)
{
    if (g_voice_chat.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    // 初始化麦克风驱动
    esp_err_t ret = mico_driver_init();
    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init mico driver");
        return ret;
    }

    ret = voice_chat_client_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init voice ASR client");
        return ret;
    }

    ret = xTaskCreatePinnedToCore(
        voice_chat_task_func,
        "voice_asr_task",
        16384,
        NULL,
        tskIDLE_PRIORITY + 1,
        &g_voice_chat.task_handle,
        1
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create voice ASR task");
        voice_chat_client_deinit();
        return ESP_FAIL;
    }
    
    g_voice_chat.initialized = true;
    g_voice_chat.recording = false;
    g_voice_chat.stop_requested = false;
    
    ESP_LOGI(TAG, "Voice ASR task initialized - listening for voice...");
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
    ESP_LOGI(TAG, "Voice ASR task deinitialized");
}