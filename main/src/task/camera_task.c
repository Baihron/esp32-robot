#include "camera_task.h"
#include "frame_queue.h"
#include "esp_log.h"

#if SD_CARD
#include "fs_driver.h"
#endif

static const char *TAG = "CAMERA_CAPTURE_TASK";

extern task_status_t g_tasks;

// 摄像头捕获任务实例
static camera_task_config_t g_capture_task = {
    .task_handle = NULL,
    .capture_interval_ms = DEFAULT_CAPTURE_INTERVAL_MS,
    .frame_count = 0,
    .send_fail_count = 0
};

// 摄像头捕获任务函数
static void camera_capture_task_func(void *arg)
{
    ESP_LOGI(TAG, "Camera capture task started processing");
    camera_fb_t *fb = NULL;
    
    while (1) {
        if(g_tasks.camera_running) {
            fb = cam_capture();

            if (fb) {
                g_capture_task.frame_count++;

                UBaseType_t queue_count = frame_queue_count();

                // 如果队列满了，丢弃最旧的帧
                if (queue_count >= DEFAULT_FRAME_QUEUE_SIZE) {
                    frame_data_t *old = frame_queue_receive_data(pdMS_TO_TICKS(0));
                    if (old) {
                        frame_queue_release_data(old);
                    }
                }
                
                // 复制数据到队列
                esp_err_t send_result = frame_queue_send_copy(fb, pdMS_TO_TICKS(QUEUE_SEND_TIMEOUT_MS));

                // 无论发送成功与否，都要立即释放摄像头缓冲区
                cam_return_frame(fb);

                if (send_result != ESP_OK) {
                    g_capture_task.send_fail_count++;
                    ESP_LOGW(TAG, "Failed to send frame to queue (%d)", send_result);
                }
            } else {
                ESP_LOGW(TAG, "Failed to capture frame");
            }
        }

        // 等待下一次捕获
        vTaskDelay(pdMS_TO_TICKS(g_capture_task.capture_interval_ms));
    }
    
    ESP_LOGI(TAG, "Camera capture task stop...");
}

esp_err_t camera_capture_task_init(uint32_t capture_interval_ms,
                                   UBaseType_t priority,
                                   uint32_t stack_size,
                                   BaseType_t core_id)
{
    if (g_capture_task.task_handle != NULL) {
        ESP_LOGW(TAG, "Camera capture task already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 设置捕获间隔
    if (capture_interval_ms > 0) {
        g_capture_task.capture_interval_ms = capture_interval_ms;
    }
    
    // 确保任务未运行状态
    g_tasks.camera_running = false;
    
    // 创建任务
    BaseType_t result = xTaskCreatePinnedToCore(
        camera_capture_task_func,
        "cam_capture_task",
        stack_size ? stack_size : DEFAULT_CAPTURE_STACK_SIZE,
        NULL,
        priority ? priority : DEFAULT_CAPTURE_TASK_PRIORITY,
        &g_capture_task.task_handle,
        core_id
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create camera capture task");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

esp_err_t camera_capture_task_start(void)
{
    if (g_capture_task.task_handle == NULL) {
        ESP_LOGE(TAG, "Camera capture task not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (g_tasks.camera_running) {
        ESP_LOGW(TAG, "Camera capture task already running");
        return ESP_OK;
    }
    
    // 重置统计
    g_capture_task.frame_count = 0;
    g_capture_task.send_fail_count = 0;

    // 设置运行标志（任务函数在等待这个标志）
    g_tasks.camera_running = true;
    
    ESP_LOGI(TAG, "Camera capture task started");
    return ESP_OK;
}

esp_err_t camera_capture_task_stop(void)
{
    if (!g_tasks.camera_running) {
        ESP_LOGW(TAG, "Camera capture task already stopped");
        return ESP_OK;
    }

    g_tasks.camera_running = false;

    // 给任务一些时间自然退出
    vTaskDelay(pdMS_TO_TICKS(200));

    return ESP_OK;
}

TaskHandle_t camera_capture_task_get_handle(void)
{
    return g_capture_task.task_handle;
}

bool camera_capture_task_is_running(void)
{
    return g_tasks.camera_running;
}

void camera_capture_task_get_stats(uint32_t *total_frames, uint32_t *failed_frames)
{
    if (total_frames) *total_frames = g_capture_task.frame_count;
    if (failed_frames) *failed_frames = g_capture_task.send_fail_count;
}

void camera_capture_task_set_interval(uint32_t interval_ms)
{
    if (interval_ms > 0) {
        g_capture_task.capture_interval_ms = interval_ms;
        ESP_LOGI(TAG, "Capture interval set to %dms", interval_ms);
    }
}

void camera_capture_task_deinit(void)
{
    // 先停止任务
    camera_capture_task_stop();
    
    // 等待任务结束
    if (g_capture_task.task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(500));
        
        // 清除句柄
        g_capture_task.task_handle = NULL;
    }
    
    // 重置配置
    g_capture_task.capture_interval_ms = DEFAULT_CAPTURE_INTERVAL_MS;
    g_capture_task.frame_count = 0;
    g_capture_task.send_fail_count = 0;
    
    ESP_LOGI(TAG, "Camera capture task deinitialized");
}