#include "display_task.h"
#include "emotion_system.h"
#include "state_manager.h"

#include "button_task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "eye_tracking.h"
#include <math.h>

static const char *TAG = "DISPLAY_TASK";

extern task_status_t g_tasks;
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
    init_random();

    // 初始化表情系统
    esp_err_t emotion_ret = emotion_system_init();
    if (emotion_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize emotion system: %s", esp_err_to_name(emotion_ret));
    } else {
        ESP_LOGI(TAG, "Emotion system initialized");
    }

    // 初始化视线追踪
    eye_tracking_init(NULL);

    // 表情相关变量
    static uint32_t last_emotion_update = 0;
    static emotion_type_t current_emotion = EMOTION_NEUTRAL;
    static system_state_t last_state = STATE_SLEEP;
    static uint32_t auto_change_timer = 0;
    static uint32_t auto_change_interval = 0;
    static bool first_unlock = false;

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

            // ===== 1. 更新动画（始终执行） =====
            uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if (last_emotion_update == 0) {
                last_emotion_update = current_time;
            }
            uint32_t elapsed = current_time - last_emotion_update;
            last_emotion_update = current_time;
            emotion_update_animation(elapsed);

            // ===== 2. 检测是否需要重绘 =====
            system_state_t current_state = state_manager_get_state();
            bool should_redraw = false;

            // 2.1 系统状态变化
            if (current_state != last_state) {
                ESP_LOGI(TAG, "State changed from %d to %d", last_state, current_state);
                should_redraw = true;
                if (current_state == STATE_LOCKED) {
                    ESP_LOGI(TAG, "Entering LOCKED state, showing neutral face");
                    emotion_set_current(EMOTION_NEUTRAL);
                    current_emotion = EMOTION_NEUTRAL;
                    first_unlock = true;
                    auto_change_timer = 0;
                    // 锁定状态下禁止视线追踪
                    eye_tracking_set_enabled(false);
                } else if (current_state == STATE_UNLOCKED) {
                    // 解锁后恢复视线追踪
                    eye_tracking_set_enabled(true);
                }
                last_state = current_state;
            }

            // 2.2 表情切换标志
            if (g_emotion_change_flag != EMOTION_FLAG_NEUTRAL) {
                should_redraw = true;
                ESP_LOGI(TAG, "Emotion change flag detected: %d", g_emotion_change_flag);
                emotion_type_t new_emotion = EMOTION_HAPPY;
                if (g_emotion_change_flag == EMOTION_FLAG_RANDOM) {
                    do {
                        new_emotion = (emotion_type_t)((rand() % (EMOTION_COUNT - 2)) + 1);
                    } while (new_emotion == current_emotion || new_emotion == EMOTION_BLINKING);
                    ESP_LOGI(TAG, "Random emotion selected: %s", emotion_get_name(new_emotion));
                } else {
                    switch (g_emotion_change_flag) {
                        case EMOTION_FLAG_HAPPY:     new_emotion = EMOTION_HAPPY; break;
                        case EMOTION_FLAG_SAD:       new_emotion = EMOTION_SAD; break;
                        case EMOTION_FLAG_ANGRY:     new_emotion = EMOTION_ANGRY; break;
                        case EMOTION_FLAG_SURPRISED: new_emotion = EMOTION_SURPRISED; break;
                        case EMOTION_FLAG_SLEEPY:    new_emotion = EMOTION_SLEEPY; break;
                        case EMOTION_FLAG_LOVING:    new_emotion = EMOTION_LOVING; break;
                        case EMOTION_FLAG_CONFUSED:  new_emotion = EMOTION_CONFUSED; break;
                        case EMOTION_FLAG_LAUGHING:  new_emotion = EMOTION_LAUGHING; break;
                        case EMOTION_FLAG_BLINKING:  new_emotion = EMOTION_BLINKING; break;
                        default: new_emotion = EMOTION_HAPPY; break;
                    }
                }
                emotion_set_current(new_emotion);
                current_emotion = new_emotion;
                g_emotion_change_flag = EMOTION_FLAG_NEUTRAL;
                auto_change_interval = (uint32_t)((60000 + (rand() % 60001)) / portTICK_PERIOD_MS);
                auto_change_timer = xTaskGetTickCount();
                ESP_LOGI(TAG, "Emotion changed to: %s", emotion_get_name(new_emotion));
            }

            // 2.3 眨眼状态变化
            bool current_blink = emotion_is_blinking();
            if (current_blink != g_display_task.last_blink_state) {
                should_redraw = true;
                g_display_task.last_blink_state = current_blink;
            }

            // 2.4 视线偏移变化（仅在解锁状态且有人脸时）
            if (current_state == STATE_UNLOCKED && eye_tracking_has_face()) {
                float ox, oy;
                eye_tracking_get_offset(&ox, &oy);
                if (fabsf(ox - g_display_task.last_offset_x) > 2.0f ||
                    fabsf(oy - g_display_task.last_offset_y) > 2.0f) {
                    should_redraw = true;
                    g_display_task.last_offset_x = ox;
                    g_display_task.last_offset_y = oy;
                }
            }

            // 2.5 首次解锁后的自动切换计时到期
            if (current_state == STATE_UNLOCKED && !first_unlock) {
                if (xTaskGetTickCount() - auto_change_timer >= auto_change_interval) {
                    should_redraw = true;
                    emotion_type_t new_emotion;
                    do {
                        new_emotion = (emotion_type_t)((rand() % (EMOTION_COUNT - 2)) + 1);
                    } while (new_emotion == current_emotion || new_emotion == EMOTION_BLINKING);
                    ESP_LOGI(TAG, "Auto-change timer expired, switching to: %s", emotion_get_name(new_emotion));
                    emotion_set_current(new_emotion);
                    current_emotion = new_emotion;
                    auto_change_interval = (uint32_t)((60000 + (rand() % 60001)) / portTICK_PERIOD_MS);
                    auto_change_timer = xTaskGetTickCount();
                    ESP_LOGI(TAG, "Next auto-change in %.1f seconds", 
                             auto_change_interval * portTICK_PERIOD_MS / 1000.0f);
                }
            }

            // 2.6 如果第一次解锁，也触发重绘
            if (current_state == STATE_UNLOCKED && first_unlock) {
                should_redraw = true;
                ESP_LOGI(TAG, "First unlock since lock, showing happy face");
                emotion_set_current(EMOTION_HAPPY);
                current_emotion = EMOTION_HAPPY;
                first_unlock = false;
                auto_change_interval = (uint32_t)((60000 + (rand() % 60001)) / portTICK_PERIOD_MS);
                auto_change_timer = xTaskGetTickCount();
            }

            // ===== 3. 执行绘制和刷新 =====
            if (should_redraw) {
                // 清屏（使用 memset 加速）
                int total_bytes = g_display_task.width * g_display_task.height * sizeof(uint16_t);
                memset(g_display_task.framebuffer, 0xFF, total_bytes); // 白色背景

                // 根据状态绘制表情
                if (current_state == STATE_UNLOCKED) {
                    emotion_draw_to_buffer(g_display_task.framebuffer, 
                                          g_display_task.width, 
                                          g_display_task.height);
                } else if (current_state == STATE_LOCKED) {
                    emotion_draw_to_buffer(g_display_task.framebuffer, 
                                          g_display_task.width, 
                                          g_display_task.height);
                } else {
                    // 其他状态：保持全黑（休眠等）
                    memset(g_display_task.framebuffer, 0x00, total_bytes);
                    ESP_LOGI(TAG, "display_running Current state STATE_ELSE");
                }

                // 刷新显示
                vTaskDelay(pdMS_TO_TICKS(2));
                esp_err_t flush_ret = flush_display();
                if (flush_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Flush failed with error: %s", esp_err_to_name(flush_ret));
                    vTaskDelay(pdMS_TO_TICKS(10));
                    flush_display();
                }
                vTaskDelay(pdMS_TO_TICKS(10));

                // 降低帧率：每次重绘后稍微等待
                vTaskDelay(pdMS_TO_TICKS(30));
            } else {
                // 无变化时延长等待（约 10 FPS）
                vTaskDelay(pdMS_TO_TICKS(90));
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
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
    // vTaskDelay(pdMS_TO_TICKS(50));
    // flush_display();

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
