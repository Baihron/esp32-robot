#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "system_init.h"
#include "task_controller.h"
#include "config.h"

#ifdef CONFIG_DEBUG_PRINT
#include "esp_heap_caps.h"
#include "frame_queue.h"
#endif

#include "common_type.h"
#include "wifi_sta.h"
#include "voice_chat_task.h"
#include "mico_driver.h"

static const char *TAG = "APP";

// 系统状态监控任务
static void system_monitor_task(void)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();

#ifdef CONFIG_DEBUG_PRINT
    uint32_t counter = 0;
#endif

    while (1) {
        xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));

#ifdef CONFIG_DEBUG_PRINT
        counter++;
        
        // 每PERFORMANCE_SAMPLING_RATE秒打印一次系统状态
        if (counter % PERFORMANCE_SAMPLING_RATE == 0) {
            counter = 1;
            
            // 获取系统状态信息
            UBaseType_t queue_count = frame_queue_count();
            
            ESP_LOGI(TAG, "=== System Status ===");
            ESP_LOGI(TAG, "System State: %s", system_get_status_string());
            ESP_LOGI(TAG, "System Initialized: %s", system_is_initialized() ? "YES" : "NO");
            ESP_LOGI(TAG, "Powered ON: %s", state_manager_is_powered_on() ? "YES" : "NO");
            ESP_LOGI(TAG, "Face Enrolled: %s", state_manager_is_face_enrolled() ? "YES" : "NO");
            ESP_LOGI(TAG, "Face Detection Needed: %s", state_manager_need_face_detection() ? "YES" : "NO");
            ESP_LOGI(TAG, "Free heap: %d bytes", esp_get_free_heap_size());
            ESP_LOGI(TAG, "PSRAM free: %d bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            ESP_LOGI(TAG, "Internal free: %d bytes", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            ESP_LOGI(TAG, "Frame Queue: %d frames waiting", queue_count);
            
            // 获取任务状态
            bool camera_running, display_running, face_detection_running;
            task_controller_get_status(&camera_running, &display_running, &face_detection_running);
            
            ESP_LOGI(TAG, "Tasks - Camera: %s, Display: %s, FaceDetection: %s",
                     camera_running ? "RUNNING" : "STOPPED",
                     display_running ? "RUNNING" : "STOPPED",
                     face_detection_running ? "RUNNING" : "STOPPED");
            
            // 获取系统配置
            const system_config_t* config = system_get_config();
            ESP_LOGI(TAG, "Config - Camera: %s, Display: %s, SD Card: %s, FaceDetect: %s",
                     config->camera_enabled ? "ENABLED" : "DISABLED",
                     config->display_enabled ? "ENABLED" : "DISABLED",
                     config->sd_card_enabled ? "ENABLED" : "DISABLED",
                     config->face_detect_enabled ? "ENABLED" : "DISABLED");
            
            
            ESP_LOGI(TAG, "=====================");
        }
#endif
    }
}

static void voice_init_task(void *arg)
{
    esp_err_t ret = voice_chat_task_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Voice chat init failed: %s", esp_err_to_name(ret));
    }
    vTaskDelete(NULL);  // 初始化完就删除自己
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Black Camera System...");

    wifi_init_sta();

    esp_err_t ret = mico_driver_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Microphone init failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Microphone initialized successfully");
    }

    // 3. 初始化语音聊天任务（自动检测声音）
    xTaskCreate(&voice_init_task, "voice_init", 16384, NULL, 5, NULL);

    ESP_LOGI(TAG, "Voice chat main task created");

    // 系统初始化
    // esp_err_t ret = system_init_all();
    // if (ret != ESP_OK) {
    //     ESP_LOGE(TAG, "System initialization failed: %s", esp_err_to_name(ret));
    //     return;
    // }

    // ESP_LOGI(TAG, "System initialized successfully");
    // 启动任务控制器
    // task_controller_start();

#ifdef CONFIG_DEBUG_PRINT
    ESP_LOGI(TAG, "System started. Current state: %s", system_get_status_string());
    ESP_LOGI(TAG, "Press and hold BOOT button to power on/off");
    ESP_LOGI(TAG, "In UNLOCKED state, double-click BOOT button to enroll face");
#endif

    // 启动系统监控任务
    xTaskCreatePinnedToCore(
        (TaskFunction_t)system_monitor_task,
        "system_monitor",
        4096,
        NULL,
        tskIDLE_PRIORITY + 1,
        NULL,
        tskNO_AFFINITY
    );

    // 主循环（保持空闲）
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        // 示例：如果系统未初始化，尝试恢复
        // if (!system_is_initialized()) {
        //     ESP_LOGW(TAG, "System not initialized, attempting recovery...");
        //     ret = system_init_all();
        //     if (ret == ESP_OK) {
        //         ESP_LOGI(TAG, "System recovery successful");
        //     } else {
        //         ESP_LOGE(TAG, "System recovery failed: %s", esp_err_to_name(ret));
        //     }
        // }
    }
}