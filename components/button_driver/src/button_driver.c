#include "button_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "BUTTON_DRIVER";

// 按钮配置结构
typedef struct {
    gpio_num_t gpio_num;
    uint8_t active_level;      // 按下时的电平（0或1）
    uint8_t last_level;        // 上次读取的电平
    uint8_t debounce_level;    // 去抖后的电平
    uint32_t press_start_time; // 按下开始时间
    uint32_t click_count;      // 点击计数
    uint32_t last_click_time;  // 上次点击时间
    uint8_t long_press_detected; // 长按已检测标志
} button_config_t;

// 按钮配置数组
static button_config_t button_configs[BUTTON_MAX] = {
    {GPIO_NUM_0, 0, 0, 0, 0, 0, 0, 0},   // BUTTON_BOOT
};

// 回调函数数组
static struct {
    button_callback_t callbacks[BUTTON_EVENT_MAX];
    void* user_data[BUTTON_EVENT_MAX];
} button_callbacks[BUTTON_MAX];

// 任务句柄和队列
static TaskHandle_t button_task_handle = NULL;
static QueueHandle_t button_event_queue = NULL;

// 事件结构
typedef struct {
    button_id_t button_id;
    button_event_t event;
} button_event_msg_t;

// 去抖函数
static uint8_t debounce_filter(button_id_t button_id, uint8_t current_level) {
    static uint8_t filter_buffer[BUTTON_MAX][5] = {0};
    static uint8_t filter_index[BUTTON_MAX] = {0};
    
    // 更新滤波缓冲区
    filter_buffer[button_id][filter_index[button_id]] = current_level;
    filter_index[button_id] = (filter_index[button_id] + 1) % 5;
    
    // 计算平均值
    uint8_t sum = 0;
    for (int i = 0; i < 5; i++) {
        sum += filter_buffer[button_id][i];
    }
    
    return (sum > 2) ? 1 : 0;  // 多数表决
}

// 发送事件到队列
static void send_button_event(button_id_t button_id, button_event_t event) {
    button_event_msg_t msg = {
        .button_id = button_id,
        .event = event
    };
    
    if (button_event_queue != NULL) {
        xQueueSend(button_event_queue, &msg, 0);
    }
}

// 处理按钮状态
static void process_button_state(button_id_t button_id) {
    button_config_t* btn = &button_configs[button_id];
    static uint32_t last_hold_time[BUTTON_MAX] = {0};
    static uint32_t last_repeat_time[BUTTON_MAX] = {0};
    
    uint32_t current_time = esp_timer_get_time() / 1000; // 转换为毫秒
    
    // 读取当前电平
    uint8_t current_level = gpio_get_level(btn->gpio_num);
    
    // 去抖处理
    uint8_t filtered_level = debounce_filter(button_id, current_level);
    btn->last_level = current_level;
    
    // 检测电平变化
    if (filtered_level != btn->debounce_level) {
        btn->debounce_level = filtered_level;
        
        if (filtered_level == btn->active_level) {
            // 按钮按下
            btn->press_start_time = current_time;
            btn->long_press_detected = 0;
            last_hold_time[button_id] = 0;
            last_repeat_time[button_id] = 0;
            send_button_event(button_id, BUTTON_EVENT_PRESS_DOWN);
        } else {
            // 按钮释放
            uint32_t press_duration = current_time - btn->press_start_time;
            send_button_event(button_id, BUTTON_EVENT_PRESS_UP);
            
            // 检测点击事件
            if (press_duration < 1000) { // 短按
                // 增加点击计数
                btn->click_count++;
                btn->last_click_time = current_time;
                
                // 如果这是第一次点击，设置定时器检测双击
                if (btn->click_count == 1) {
                    // 不立即发送单击事件，等待可能的第二次点击
                } 
                // 如果是第二次点击，检查时间间隔
                else if (btn->click_count == 2) {
                    uint32_t click_interval = current_time - btn->last_click_time;
                    if (click_interval < 500) { // 双击时间间隔小于500ms
                        send_button_event(button_id, BUTTON_EVENT_DOUBLE_CLICK);
                        btn->click_count = 0; // 重置点击计数
                    } else {
                        // 时间间隔太长，认为是两次独立的单击
                        send_button_event(button_id, BUTTON_EVENT_SINGLE_CLICK);
                        btn->click_count = 1; // 保留这次点击作为第一次点击
                        btn->last_click_time = current_time;
                    }
                }
            } else {
                // 长按释放，不触发点击事件
                btn->click_count = 0;
            }
        }
    } else if (filtered_level == btn->active_level) {
        // 按钮保持按下状态
        uint32_t press_duration = current_time - btn->press_start_time;
        
        // 检测长按开始（只触发一次）
        if (!btn->long_press_detected && press_duration > 1000) {
            send_button_event(button_id, BUTTON_EVENT_LONG_PRESS_START);
            btn->long_press_detected = 1;
            // 长按开始，清除点击计数
            btn->click_count = 0;
        }
        
        // 检测长按保持（周期性触发）
        if (btn->long_press_detected && press_duration > 2000) {
            // 每500ms触发一次长按保持事件
            if (current_time - last_hold_time[button_id] > 500) {
                send_button_event(button_id, BUTTON_EVENT_LONG_PRESS_HOLD);
                last_hold_time[button_id] = current_time;
            }
        }
        
        // 检测重复按下（快速连按）
        if (press_duration > 300 && press_duration < 1000) {
            if (current_time - last_repeat_time[button_id] > 200) {
                send_button_event(button_id, BUTTON_EVENT_PRESS_REPEAT);
                last_repeat_time[button_id] = current_time;
            }
        }
    } else {
        // 按钮保持释放状态
        // 检测单击超时（释放后500ms内没有第二次点击）
        if (btn->click_count == 1) {
            if (current_time - btn->last_click_time > 500) {
                send_button_event(button_id, BUTTON_EVENT_SINGLE_CLICK);
                btn->click_count = 0;
            }
        }
    }
}

// 按钮任务
static void button_task(void* arg) {
    ESP_LOGI(TAG, "Button task started");
    
    button_event_msg_t msg;
    
    while (1) {
        // 处理所有按钮
        for (int i = 0; i < BUTTON_MAX; i++) {
            process_button_state(i);
        }
        
        // 处理事件队列
        if (xQueueReceive(button_event_queue, &msg, pdMS_TO_TICKS(10)) == pdTRUE) {
            // 调用注册的回调函数
            if (msg.button_id < BUTTON_MAX && msg.event < BUTTON_EVENT_MAX) {
                button_callback_t callback = button_callbacks[msg.button_id].callbacks[msg.event];
                if (callback != NULL) {
                    callback(msg.button_id, msg.event, 
                            button_callbacks[msg.button_id].user_data[msg.event]);
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10)); // 10ms扫描周期
    }
}

// 初始化按钮驱动
void button_driver_init(void) {
    // 初始化GPIO
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = 0,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE, // 启用上拉电阻
    };

    // 配置每个按钮的GPIO
    for (int i = 0; i < BUTTON_MAX; i++) {
        io_conf.pin_bit_mask |= (1ULL << button_configs[i].gpio_num);

        // 初始化按钮状态
        button_configs[i].last_level = gpio_get_level(button_configs[i].gpio_num);
        button_configs[i].debounce_level = button_configs[i].last_level;
        button_configs[i].press_start_time = 0;
        button_configs[i].click_count = 0;
        button_configs[i].last_click_time = 0;
        button_configs[i].long_press_detected = 0;

        // 清空回调函数
        for (int j = 0; j < BUTTON_EVENT_MAX; j++) {
            button_callbacks[i].callbacks[j] = NULL;
            button_callbacks[i].user_data[j] = NULL;
        }
    }

    gpio_config(&io_conf);

    // 创建事件队列
    button_event_queue = xQueueCreate(10, sizeof(button_event_msg_t));

    // 创建按钮任务
    xTaskCreate(button_task, "button_task", 4096, NULL, 5, &button_task_handle);

    ESP_LOGI(TAG, "Button driver initialized with %d buttons", BUTTON_MAX);
}

// 注册按钮事件回调
void button_register_callback(button_id_t button_id, button_event_t event, 
                              button_callback_t callback, void* user_data) {
    if (button_id < BUTTON_MAX && event < BUTTON_EVENT_MAX) {
        button_callbacks[button_id].callbacks[event] = callback;
        button_callbacks[button_id].user_data[event] = user_data;
    }
}

// 获取按钮电平
int button_get_level(button_id_t button_id) {
    if (button_id < BUTTON_MAX) {
        return gpio_get_level(button_configs[button_id].gpio_num);
    }
    return -1;
}