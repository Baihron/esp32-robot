#include "display_task.h"

#include "esp_log.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <math.h>

static const char *TAG = "DISPLAY_TASK";

extern volatile emotion_change_flag_t g_emotion_change_flag;

// 显示任务配置
static struct {
    TaskHandle_t task_handle;
    uint32_t display_count;        // 统计：显示的帧数
    uint32_t process_time_ms;      // 上次处理时间（毫秒）
    uint16_t *framebuffer;         // 显示缓冲区（从dis_driver获取）
    uint16_t width;                 // LCD宽度
    uint16_t height;                // LCD高度
    bool random_initialized;
    uint32_t error_count;          // 错误计数
    uint32_t last_diagnose_time;   // 上次诊断时间

    bool need_redraw;               // 是否需要重绘
    bool last_blink_state;          // 上一次眨眼状态
    float last_offset_x;            // 上一次视线偏移X
    float last_offset_y;            // 上一次视线偏移Y
} g_display_task = {
    .task_handle = NULL,
    .display_count = 0,
    .process_time_ms = 0,
    .framebuffer = NULL,
    .width = 0,
    .height = 0,
    .random_initialized = false,
    .error_count = 0,
    .last_diagnose_time = 0,
    .need_redraw = true,            // 首次需要绘制
    .last_blink_state = false,
    .last_offset_x = 0.0f,
    .last_offset_y = 0.0f
};

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
    // 主循环
    while (1) {

        int total_pixels = g_display_task.width * g_display_task.height;
        for (int i = 0; i < total_pixels; i++) {
            g_display_task.framebuffer[i] = 0x1a1a;
        }

        flush_display();
        vTaskDelay(pdMS_TO_TICKS(1000));
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
    ESP_LOGI(TAG, "Init LCD initialized: %dx%d, buffer at %p", g_display_task.width, g_display_task.height, g_display_task.framebuffer);

    // 清屏为黑色
    uint16_t black = 0xffff;
    int total_pixels = g_display_task.width * g_display_task.height;
    for (int i = 0; i < total_pixels; i++) {
        g_display_task.framebuffer[i] = black;
    }

    // 首次刷新
    vTaskDelay(pdMS_TO_TICKS(100));  // 等待硬件稳定
    flush_display();
    vTaskDelay(pdMS_TO_TICKS(100));  // 等待刷新完成

    // 任务未运行状态
    // .display_running = false;

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

    ESP_LOGI(TAG, "Display task started");

    // 清屏
    // uint16_t white = 0xAAAA;
    // int total_pixels = g_display_task.width * g_display_task.height;
    // for (int i = 0; i < total_pixels; i++) {
    //     g_display_task.framebuffer[i] = white;
    // }

    // 等待任务开始运行
    // vTaskDelay(pdMS_TO_TICKS(50));
    // flush_display();

    return ESP_OK;
}

esp_err_t display_task_stop(void)
{
    // g_tasks.display_running = false;

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
    return true; // g_tasks.display_running;
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
