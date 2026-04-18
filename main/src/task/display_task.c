#include "display_task.h"
#include "emotion_system.h"
#include "state_manager.h"

#include "button_task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <time.h>
#include <string.h>

static const char *TAG = "DISPLAY_TASK";

extern task_status_t g_tasks;

// 显示任务配置
volatile static struct {
    TaskHandle_t task_handle;
    uint32_t display_count;        // 统计：显示的帧数
    uint32_t process_time_ms;      // 上次处理时间（毫秒）
    uint16_t *framebuffer;         // 显示缓冲区（从dis_driver获取）
    uint16_t width;                 // LCD宽度
    uint16_t height;                // LCD高度
    bool random_initialized;
    uint32_t error_count;          // 错误计数
    uint32_t last_diagnose_time;   // 上次诊断时间
} g_display_task = {
    .task_handle = NULL,
    .display_count = 0,
    .process_time_ms = 0,
    .framebuffer = NULL,
    .width = 0,
    .height = 0,
    .random_initialized = false,
    .error_count = 0,
    .last_diagnose_time = 0
};

// 按钮标志（从其他文件引入）
// extern uint8_t btn0_press_flag;

// 初始化随机数生成器
static void init_random(void)
{
    if (!g_display_task.random_initialized) {
        srand((unsigned int)esp_timer_get_time());
        g_display_task.random_initialized = true;
    }
}

// 刷新屏幕
static esp_err_t flush_display(void)
{
    TickType_t start = xTaskGetTickCount();
    esp_err_t ret = ESP_FAIL;
    
    if (g_display_task.framebuffer) {
        ret = dis_flush();
        if (ret == ESP_OK) {
            g_display_task.display_count++;
            g_display_task.error_count = 0;  // 重置错误计数
        } else {
            ESP_LOGW(TAG, "Flush failed: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGW(TAG, "Framebuffer is NULL");
        g_display_task.error_count++;
        
        // 尝试重新获取帧缓冲区
        g_display_task.framebuffer = dis_get_framebuffer();
        if (g_display_task.framebuffer) {
            ESP_LOGI(TAG, "Recovered framebuffer at %p", g_display_task.framebuffer);
        }
    }

    TickType_t end = xTaskGetTickCount();
    g_display_task.process_time_ms = (end - start) * portTICK_PERIOD_MS;

    return ret;
}

// 显示任务函数
static void display_task_func(void *arg)
{
    // 初始化随机数
    init_random();

    // 初始化表情系统
    esp_err_t emotion_ret = emotion_system_init();
    if (emotion_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize emotion system: %s", esp_err_to_name(emotion_ret));
    } else {
        ESP_LOGI(TAG, "Emotion system initialized");
    }
    
    // 表情相关变量
    static uint32_t last_emotion_update = 0;
    static uint32_t emotion_change_timer = 0;
    static bool first_frame = true;

    // 主循环
    while (1) {
        if (g_tasks.display_running) {
            // 检查帧缓冲区有效性
            if (!g_display_task.framebuffer) {
                ESP_LOGE(TAG, "Framebuffer is NULL, attempting recovery");
                g_display_task.framebuffer = dis_get_framebuffer();
                if (!g_display_task.framebuffer) {
                    ESP_LOGE(TAG, "Failed to get framebuffer, waiting...");
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    continue;
                }
                ESP_LOGI(TAG, "Recovered framebuffer at %p", g_display_task.framebuffer);
            }

            // 获取当前系统状态
            system_state_t current_state = state_manager_get_state();

            // 根据系统状态决定显示内容
            if (current_state == STATE_UNLOCKED) {
                // 解锁状态：显示表情
                
                // 更新表情动画
                uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
                if (last_emotion_update == 0) {
                    last_emotion_update = current_time;
                }
                uint32_t elapsed = current_time - last_emotion_update;
                last_emotion_update = current_time;
                
                emotion_update_animation(elapsed);
                
                // 随机切换表情（每10-30秒）
                emotion_change_timer += elapsed;
                if (emotion_change_timer >= 10000) { // 10秒
                    emotion_change_timer = 0;
                    
                    // 随机选择表情（排除眨眼动画）
                    emotion_type_t random_emotion = (rand() % (EMOTION_COUNT - 2)) + 1;
                    emotion_set_current(random_emotion);
                    
                    ESP_LOGI(TAG, "Random emotion change to: %s", 
                            emotion_get_name(random_emotion));
                }

                int total_pixels = g_display_task.width * g_display_task.height;
                for (int i = 0; i < total_pixels; i++) {
                    g_display_task.framebuffer[i] = 0xFFFF; // 白色背景
                }

                // 绘制表情
                emotion_draw_to_buffer(g_display_task.framebuffer, 
                                      g_display_task.width, 
                                      g_display_task.height);

                // 添加刷新前的延迟
                vTaskDelay(pdMS_TO_TICKS(2));

                // 刷新显示
                esp_err_t flush_ret = flush_display();

                if (flush_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Flush failed with error: %s", esp_err_to_name(flush_ret));
                    vTaskDelay(pdMS_TO_TICKS(10));
                    flush_display();
                }

                // 添加刷新后的延迟
                vTaskDelay(pdMS_TO_TICKS(10));

            } else if (current_state == STATE_LOCKED) {
                // 锁定状态：显示锁定界面
                // 清屏为灰色
                int total_pixels = g_display_task.width * g_display_task.height;
                for (int i = 0; i < total_pixels; i++) {
                    g_display_task.framebuffer[i] = 0xFFFF; // 灰色
                }

                // 刷新显示
                vTaskDelay(pdMS_TO_TICKS(2));
                flush_display();
                vTaskDelay(pdMS_TO_TICKS(10));

            } else {
                int total_pixels = g_display_task.width * g_display_task.height;
                for (int i = 0; i < total_pixels; i++) {
                    g_display_task.framebuffer[i] = 0x0000;
                }

                vTaskDelay(pdMS_TO_TICKS(2));
                flush_display();
                vTaskDelay(pdMS_TO_TICKS(10));
            }

            // 控制刷新频率（约10FPS）
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ============================================
// 任务管理API
// ============================================

esp_err_t display_task_init(UBaseType_t priority,
                           uint32_t stack_size,
                           BaseType_t core_id)
{
    if (g_display_task.task_handle != NULL) {
        ESP_LOGW(TAG, "Display task already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // 初始化LCD驱动（使用默认配置）
    dis_config_t config = {
        .sclk_gpio = DISPLAY_SCLK_GPIO,
        .mosi_gpio = DISPLAY_MOSI_GPIO,
        .dc_gpio = DISPLAY_DC_GPIO,
        .cs_gpio = DISPLAY_CS_GPIO,
        .rst_gpio = DISPLAY_RST_GPIO,
        .en_gpio = DISPLAY_EN_GPIO,
        .width = DISPLAY_WIDTH,
        .height = DISPLAY_HEIGHT,
        .pclk_hz = DISPLAY_PCLK_HZ,
    };

    esp_err_t ret = dis_driver_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LCD driver: %s", esp_err_to_name(ret));
        return ret;
    }

    // 获取帧缓冲区和屏幕尺寸
    g_display_task.framebuffer = dis_get_framebuffer();
    if (!g_display_task.framebuffer) {
        ESP_LOGE(TAG, "Failed to get framebuffer");
        return ESP_ERR_NO_MEM;
    }

    dis_get_size(&g_display_task.width, &g_display_task.height);
    ESP_LOGI(TAG, "LCD initialized: %dx%d, buffer at %p", g_display_task.width, g_display_task.height, g_display_task.framebuffer);

    // 清屏为黑色
    uint16_t black = 0x0000;
    int total_pixels = g_display_task.width * g_display_task.height;
    for (int i = 0; i < total_pixels; i++) {
        g_display_task.framebuffer[i] = black;
    }

    // 首次刷新
    vTaskDelay(pdMS_TO_TICKS(100));  // 等待硬件稳定
    flush_display();
    vTaskDelay(pdMS_TO_TICKS(100));  // 等待刷新完成

    // 任务未运行状态
    g_tasks.display_running = false;

    // 创建任务
    BaseType_t result = xTaskCreatePinnedToCore(
        display_task_func,
        "display_task",
        stack_size ? stack_size : 4096,
        NULL,
        priority ? priority : 5,
        &g_display_task.task_handle,
        core_id
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create display task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Display task created successfully");

    return ESP_OK;
}

esp_err_t display_task_start(void)
{
    if (g_display_task.task_handle == NULL) {
        ESP_LOGE(TAG, "Display task not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (g_tasks.display_running) {
        ESP_LOGW(TAG, "Display task already running");
        return ESP_OK;
    }

    g_tasks.display_running = true;
    ESP_LOGI(TAG, "Display task started");

    // 清屏
    // uint16_t white = 0xAAAA;
    // int total_pixels = g_display_task.width * g_display_task.height;
    // for (int i = 0; i < total_pixels; i++) {
    //     g_display_task.framebuffer[i] = white;
    // }

    // 等待任务开始运行
    vTaskDelay(pdMS_TO_TICKS(50));
    flush_display();

    return ESP_OK;
}

esp_err_t display_task_stop(void)
{
    g_tasks.display_running = false;

    uint16_t black = 0x0000;
    int total_pixels = g_display_task.width * g_display_task.height;
    for (int i = 0; i < total_pixels; i++) {
        g_display_task.framebuffer[i] = black;
    }

    vTaskDelay(pdMS_TO_TICKS(10));  // 等待硬件稳定
    flush_display();
    vTaskDelay(pdMS_TO_TICKS(100));  // 等待刷新完成

    ESP_LOGI(TAG, "Display task stopped");
    return ESP_OK;
}

TaskHandle_t display_task_get_handle(void)
{
    return g_display_task.task_handle;
}

bool display_task_is_running(void)
{
    return g_tasks.display_running;
}

void display_task_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing display task...");

    // 停止任务
    display_task_stop();

    // 等待任务退出
    if (g_display_task.task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(200));
        g_display_task.task_handle = NULL;
    }

    // 反初始化LCD驱动
    dis_driver_deinit();

    // 重置状态
    g_display_task.framebuffer = NULL;
    g_display_task.width = 0;
    g_display_task.height = 0;
    g_display_task.display_count = 0;
    g_display_task.error_count = 0;

    ESP_LOGI(TAG, "Display task deinitialized");
}

// ============================================
// 缓冲区管理
// ============================================

uint16_t *display_task_get_framebuffer(void)
{
    return g_display_task.framebuffer;
}

void display_task_get_lcd_size(uint16_t *width, uint16_t *height)
{
    if (width) *width = g_display_task.width;
    if (height) *height = g_display_task.height;
}

// ============================================
// 统计信息
// ============================================

void display_task_get_stats(uint32_t *displayed, uint32_t *process_time, uint32_t *error_count)
{
    if (displayed) *displayed = g_display_task.display_count;
    if (process_time) *process_time = g_display_task.process_time_ms;
    if (error_count) *error_count = g_display_task.error_count;
}
