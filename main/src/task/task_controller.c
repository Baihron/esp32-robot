#include "task_controller.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 任务相关头文件
#include "camera_task.h"
#include "display_task.h"
#include "face_detect_task.h"
#include "frame_queue.h"
#include "config.h"
#include "common_type.h"

static const char *TAG = "TASK_CONTROLLER";

task_status_t g_tasks = {0};

extern char* STATE_NAMES;
// 状态处理函数
static void enter_sleep_mode(void)
{
    ESP_LOGI(TAG, "Entering sleep mode...");

    // 停止所有任务
    if (g_tasks.face_detection_running) {
        ESP_LOGI(TAG, "Stopping face detection task");
        face_detect_task_stop();
    }

    if (g_tasks.camera_running) {
        ESP_LOGI(TAG, "Stopping camera task");
        camera_capture_task_stop();
    }

    if (g_tasks.display_running) {
        ESP_LOGI(TAG, "Stopping display task");
        display_task_stop();
    }

    // 清空帧队列
    frame_queue_clear();

    ESP_LOGI(TAG, "Sleep mode entered"); 
}

static void enter_locked_mode(void)
{
    ESP_LOGI(TAG, "Entering locked mode...");

    // 初始化并启动显示任务
    if (!g_tasks.display_initialized) {
        ESP_LOGI(TAG, "Initializing display task");
        if (display_task_init(tskIDLE_PRIORITY + 3, 4096, 0) == ESP_OK) {
            g_tasks.display_initialized = true;
        }
    }

    if (g_tasks.display_running) {
        ESP_LOGI(TAG, "start display task");
        if (display_task_start() == ESP_OK) {
            ESP_LOGI(TAG, "Display task started");
        }
    }

    // 初始化并启动摄像头任务
    if (!g_tasks.camera_initialized) {
        ESP_LOGI(TAG, "Initializing camera task");
        if (camera_capture_task_init(
            DEFAULT_CAPTURE_INTERVAL_MS,
            tskIDLE_PRIORITY + 2,
            4096,
            tskNO_AFFINITY) == ESP_OK) {
            g_tasks.camera_initialized = true;
        }
    }
    
    if (!g_tasks.camera_running) {
        ESP_LOGI(TAG, "Starting camera task");
        if (camera_capture_task_start() == ESP_OK) {
            g_tasks.camera_running = true;
        }
    }

    // 启动人脸检测任务（用于解锁）
    if (!g_tasks.face_detection_initialized) {
        g_tasks.face_detection_initialized = true;
    }
    
    if (!g_tasks.face_detection_running) {
        ESP_LOGI(TAG, "Starting face detection for unlock");
        face_detect_task_start();
        g_tasks.face_detection_running = true;
    }

    ESP_LOGI(TAG, "Locked mode entered, waiting for face recognition...");
}

static void enter_unlocked_mode(void)
{
    ESP_LOGI(TAG, "Entering unlocked mode...");

    // 确保显示任务运行
    if (!g_tasks.display_initialized) {
        if (display_task_init(tskIDLE_PRIORITY + 3, 4096, 0) == ESP_OK) {
            g_tasks.display_initialized = true;
        }
    }

    if (!g_tasks.display_running) {
        if (display_task_start() == ESP_OK) {
            g_tasks.display_running = true;
        }
    }

    // 确保摄像头任务运行
    if (!g_tasks.camera_initialized) {
        if (camera_capture_task_init(
            DEFAULT_CAPTURE_INTERVAL_MS,
            tskIDLE_PRIORITY + 2,
            4096,
            tskNO_AFFINITY) == ESP_OK) {
            g_tasks.camera_initialized = true;
        }
    }

    if (!g_tasks.camera_running) {
        if (camera_capture_task_start() == ESP_OK) {
            g_tasks.camera_running = true;
        }
    }

   if (!g_tasks.face_detection_initialized) {
        g_tasks.face_detection_initialized = true;
    }
    
    if (!g_tasks.face_detection_running) {
        ESP_LOGI(TAG, "Starting face detection for unlock");
        face_detect_task_start();
        g_tasks.face_detection_running = true;
    }
    
    ESP_LOGI(TAG, "Unlocked mode entered, system ready");
}

volatile bool flag_face_enrolling = false;

static void enter_face_enrolling_mode(void)
{
    ESP_LOGI(TAG, "Entering face enrolling mode...");

    flag_face_enrolling = true;

    // 确保显示任务运行
    if (!g_tasks.display_running && g_tasks.display_initialized) {
        display_task_start();
        g_tasks.display_running = true;
    }
    
    // 确保摄像头任务运行
    if (!g_tasks.camera_running && g_tasks.camera_initialized) {
        camera_capture_task_start();
        g_tasks.camera_running = true;
    }
    
    // 设置人脸检测任务为录入模式
    if (g_tasks.face_detection_initialized) {
        ESP_LOGI(TAG, "Setting face detection to enroll mode");
        // face_detection_set_mode(ENROLL_MODE);
        g_tasks.face_detection_running = true;
    }
    
    // 显示人脸录入界面（需要实现）
    // display_task_show_enroll_screen();
    
    ESP_LOGI(TAG, "Face enrolling mode entered, ready to enroll");
}

static void enter_shutting_down_mode(void)
{
    ESP_LOGI(TAG, "Entering shutting down mode...");
    
    // 显示关机动画（需要实现）
    // display_task_show_shutdown_animation();
    
    // 停止所有任务
    enter_sleep_mode();
    
    vTaskDelay(pdMS_TO_TICKS(1000)); // 等待1秒，模拟关机过程
    
    ESP_LOGI(TAG, "Shutdown complete");
}

// 状态变化处理
static void on_state_change(system_state_t old_state, system_state_t new_state)
{
    // 标记 old_state 为已使用，避免编译器警告
    (void)old_state;
    
    // ESP_LOGI(TAG, "Task controller handling state change: %s -> %s", 
    //          STATE_NAMES[old_state], STATE_NAMES[new_state]);
    
    // 根据新状态执行相应操作
    switch (new_state) {
        case STATE_SLEEP:
            enter_sleep_mode();
            break;
            
        case STATE_LOCKED:
            enter_locked_mode();
            break;
            
        case STATE_UNLOCKED:
            enter_unlocked_mode();
            break;
            
        case STATE_FACE_ENROLLING:
            enter_face_enrolling_mode();
            break;

        case STATE_SHUTTING_DOWN:
            enter_shutting_down_mode();
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown state: %d", new_state);
            break;
    }
}

// 初始化任务控制器
void task_controller_init(void)
{
    // 重置任务状态
    memset(&g_tasks, 0, sizeof(g_tasks));
    
    // 注册状态变化回调
    state_manager_register_callback(on_state_change);
    
    ESP_LOGI(TAG, "Task controller initialized successfully");
}

// 启动任务控制器
void task_controller_start(void)
{
    ESP_LOGI(TAG, "Starting task controller");
    
    // 初始状态为休眠，不需要启动任何任务
    // 系统将等待按钮事件来触发状态变化
    
    ESP_LOGI(TAG, "Task controller started, system in SLEEP mode");
}

// 处理状态变化（外部调用）
void task_controller_handle_state_change(system_state_t old_state, system_state_t new_state)
{
    on_state_change(old_state, new_state);
}

// 获取任务状态
void task_controller_get_status(bool *camera_running, bool *display_running, bool *face_detection_running)
{
    if (camera_running) *camera_running = g_tasks.camera_running;
    if (display_running) *display_running = g_tasks.display_running;
    if (face_detection_running) *face_detection_running = g_tasks.face_detection_running;
}

// 强制停止所有任务（紧急情况）
void task_controller_emergency_stop(void)
{
    ESP_LOGW(TAG, "EMERGENCY STOP: Stopping all tasks immediately!");
    
    // 立即停止所有任务
    enter_sleep_mode();
    
    // 强制系统进入休眠状态
    // state_manager_force_state(STATE_SLEEP);
    
    ESP_LOGW(TAG, "All tasks stopped");
}