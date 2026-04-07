#include "system_init.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

// 相关头文件
#include "cam_driver.h"
#include "dis_driver.h"
#include "dis_config.h"
#include "frame_queue.h"
#include "camera_task.h"
#include "display_task.h"
#include "button_task.h"
#include "face_detect_task.h"
#include "face_recognition_task.h"
#include "flash_driver.h"
#include "fs_driver.h"
#include "state_manager.h"
#include "task_controller.h"


static const char *TAG = "SYSTEM_INIT";

// 全局配置
static system_config_t g_system_config = {
    .camera_enabled = CAMERA_TASK,
    .display_enabled = DISPLAY_TASK,
    .sd_card_enabled = SD_CARD,
    .face_detect_enabled = FACE_DETECT_TASK
};

// 初始化状态
static bool g_system_initialized = false;

extern task_status_t g_tasks;
// 初始化摄像头系统
static esp_err_t init_camera_system(void)
{
    if (!g_system_config.camera_enabled) {
        ESP_LOGI(TAG, "Camera system disabled by config");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing camera system...");
    
    // 初始化摄像头驱动
    esp_err_t ret = cam_driver_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera driver initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Camera driver initialized successfully");
    
    // 初始化摄像头任务
    ret = camera_capture_task_init(
        DEFAULT_CAPTURE_INTERVAL_MS,
        tskIDLE_PRIORITY + 2,
        4096,
        tskNO_AFFINITY
    );
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera task initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }

    g_tasks.camera_initialized = true;
    ESP_LOGI(TAG, "Camera task initialized (not started)");
    return ESP_OK;
}

// 初始化显示系统
static esp_err_t init_display_system(void)
{
    if (!g_system_config.display_enabled) {
        ESP_LOGI(TAG, "Display system disabled by config");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing display system...");

    // 初始化显示任务
    esp_err_t ret = display_task_init(
        tskIDLE_PRIORITY + 3,
        4096,
        0
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display task initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }

    g_tasks.display_initialized = true;
    ESP_LOGI(TAG, "Display task initialized successfully");
    return ESP_OK;
}

// 初始化文件系统
static esp_err_t init_filesystem(void)
{
    face_flash_storage_init();

    if (!g_system_config.sd_card_enabled) {
        ESP_LOGI(TAG, "SD card system disabled by config");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing SD card...");

    // 初始化SD卡
    esp_err_t ret = fs_sd_card_init("/sdcard");
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SD card initialized successfully");
    } else {
        ESP_LOGW(TAG, "SD card initialization failed: %s (may not be inserted)", esp_err_to_name(ret));
    }

    return ESP_OK;
}

// 初始化人脸检测系统
static esp_err_t init_face_detection_system(void)
{
    if (!g_system_config.face_detect_enabled) {
        ESP_LOGI(TAG, "Face detection system disabled by config");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing face detection system...");
    esp_err_t ret = face_detect_task_init();
    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "Face detection task initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }

    g_tasks.face_detection_initialized = true;
    ESP_LOGI(TAG, "Face detection system ready");
    return ESP_OK;
}

// 初始化人脸识别系统
static esp_err_t init_face_recognition_system(void)
{
    ESP_LOGI(TAG, "Initializing face recognition system...");
    esp_err_t ret = face_recognition_task_init();
    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "Face recognition task initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }

    g_tasks.face_recognition_initialized = true;
    ESP_LOGI(TAG, "Face recognition system ready");
    return ESP_OK;
}

// 初始化通信队列
static esp_err_t init_communication_queues(void)
{
    ESP_LOGI(TAG, "Initializing communication queues...");
    
    // 初始化帧队列
    esp_err_t ret = frame_queue_init(DEFAULT_FRAME_QUEUE_SIZE, MAX_FRAME_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Frame queue initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Frame queue initialized successfully");
    
    return ESP_OK;
}

// 初始化输入系统
static esp_err_t init_input_system(void)
{
    ESP_LOGI(TAG, "Initializing input system...");
    
    // 初始化按钮任务
    button_task_init();
    
    ESP_LOGI(TAG, "Input system initialized");
    
    return ESP_OK;
}

// 初始化状态管理系统
static esp_err_t init_state_management(void)
{
    ESP_LOGI(TAG, "Initializing state management...");

    // 初始化状态管理器
    state_manager_init();
    
    // 初始化任务控制器
    task_controller_init();
    
    return ESP_OK;
}

// 打印系统信息
static void print_system_info(void)
{
#ifdef CONFIG_DEBUG_PRINT
    ESP_LOGI(TAG, "=== System Information ===");
    ESP_LOGI(TAG, "Camera: %s", g_system_config.camera_enabled ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "Display: %s", g_system_config.display_enabled ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "SD Card: %s", g_system_config.sd_card_enabled ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "Face Detection: %s", g_system_config.face_detect_enabled ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "Minimum free heap: %d", esp_get_minimum_free_heap_size());
    ESP_LOGI(TAG, "Current free heap: %d", esp_get_free_heap_size());
    ESP_LOGI(TAG, "PSRAM free: %d bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "Internal free: %d bytes", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
#endif
    ESP_LOGI(TAG, "=================================");
}

// 初始化整个系统
esp_err_t system_init_all(void)
{
    if (g_system_initialized) {
        ESP_LOGW(TAG, "System already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Starting system initialization...");
    
    // 初始化状态管理系统
    esp_err_t ret = init_state_management();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "State management initialization failed");
        return ret;
    }
    
    // 初始化通信队列
    ret = init_communication_queues();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Communication queues initialization failed");
        return ret;
    }
    
    // 初始化摄像头系统
    ret = init_camera_system();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera system initialization failed");
        // 可以继续初始化其他系统，或者返回错误
    }
    
    // 初始化显示系统
    ret = init_display_system();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display system initialization failed");
        // 可以继续初始化其他系统，或者返回错误
    }
    
    // 初始化文件系统
    ret = init_filesystem();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Filesystem initialization failed, continuing anyway");
    }
    
    // 初始化人脸检测系统
    ret = init_face_detection_system();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Face detection system initialization failed, continuing anyway");
    }
    
    ret = init_face_recognition_system();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Face recognition system initialization failed, continuing anyway");
    }

    // 初始化输入系统
    ret = init_input_system();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Input system initialization failed");
        return ret;
    }

    print_system_info();

    g_system_initialized = true;
    ESP_LOGI(TAG, "System initialization completed successfully!");

    return ESP_OK;
}

// 反初始化系统
esp_err_t system_deinit_all(void)
{
    if (!g_system_initialized) {
        ESP_LOGW(TAG, "System not initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting system deinitialization...");

    // 停止所有任务
    // 这里可以调用task_controller_emergency_stop()或者类似函数
    
    // 反初始化各个模块
    // 注意：反初始化顺序应该与初始化顺序相反
    
    // 反初始化按钮系统
    // 按钮系统通常不需要显式反初始化
    
    // 反初始化人脸检测系统
    if (g_system_config.face_detect_enabled) {
        // face_detect_task_stop();
    }

    // 反初始化摄像头系统
    if (g_system_config.camera_enabled) {
        camera_capture_task_deinit();
        // cam_driver_deinit(); // 如果存在的话
    }

    // 反初始化显示系统
    if (g_system_config.display_enabled) {
        display_task_deinit();
        // dis_driver_deinit(); // 如果存在的话
    }

    // 反初始化文件系统
    if (g_system_config.sd_card_enabled) {
        // fs_sd_card_deinit(); // 如果存在的话
    }

    // 反初始化通信队列
    frame_queue_deinit();
    
    // 反初始化状态管理系统
    // 注意：状态管理器和任务控制器通常不需要显式反初始化
    
    g_system_initialized = false;
    ESP_LOGI(TAG, "System deinitialization completed");
    
    return ESP_OK;
}

// 获取系统配置
const system_config_t* system_get_config(void)
{
    return &g_system_config;
}

// 检查系统是否已初始化
bool system_is_initialized(void)
{
    return g_system_initialized;
}

// 系统状态字符串
const char* system_get_status_string(void)
{
    if (!g_system_initialized) {
        return "NOT_INITIALIZED";
    }

    switch (state_manager_get_state()) {
        case STATE_SLEEP: return "SLEEP";
        case STATE_LOCKED: return "LOCKED";
        case STATE_UNLOCKED: return "UNLOCKED";
        case STATE_FACE_ENROLLING: return "FACE_ENROLLING";
        case STATE_SHUTTING_DOWN: return "SHUTTING_DOWN";
        default: return "UNKNOWN";
    }
}
