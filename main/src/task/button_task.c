#include "button_task.h"
#include "button_driver.h"
#include "esp_log.h"
#include "config.h"
#include "state_manager.h"

static const char *TAG = "BUTTON_TASK";

// 按钮状态变量
static button_state_t button_state[BUTTON_MAX] = {0};

// 按钮事件处理
static void button_event_handler(button_id_t button_id, button_event_t event, void* user_data) 
{
    // 更新内部按钮状态
    switch (event) {
        case BUTTON_EVENT_PRESS_DOWN:
            ESP_LOGI(TAG, "Button %d pressed down", button_id);
            button_state[button_id] = BUTTON_STATE_PRESSED;
            break;
            
        case BUTTON_EVENT_PRESS_UP:
            ESP_LOGI(TAG, "Button %d pressed up", button_id);
            if (button_state[button_id] == BUTTON_STATE_PRESSED) {
                button_state[button_id] = BUTTON_STATE_CLICKED;
            }
            break;
            
        case BUTTON_EVENT_SINGLE_CLICK:
            ESP_LOGI(TAG, "Button %d single click", button_id);
            button_state[button_id] = BUTTON_STATE_CLICKED;
            break;
            
        case BUTTON_EVENT_DOUBLE_CLICK:
            ESP_LOGI(TAG, "Button %d double click", button_id);
            button_state[button_id] = BUTTON_STATE_DOUBLE_CLICKED;
            
            // 处理状态机事件
            if (button_id == BUTTON_BOOT) {
                system_state_t current_state = state_manager_get_state();
                if (current_state == STATE_UNLOCKED) {
                    // 解锁状态下双击开始人脸录入
                    ESP_LOGI(TAG, "Start face enrollment");
                    state_manager_handle_event(EVENT_START_ENROLL);
                }
            }
            break;
            
        case BUTTON_EVENT_LONG_PRESS_START:
            ESP_LOGI(TAG, "Button %d long press start", button_id);
            button_state[button_id] = BUTTON_STATE_LONG_PRESSED;

            // 处理状态机事件
            if (button_id == BUTTON_BOOT) {
                system_state_t current_state = state_manager_get_state();
                
                if (current_state == STATE_SLEEP) {
                    // 休眠状态下长按开机
                    ESP_LOGI(TAG, "Power ON from sleep");
                    state_manager_handle_event(EVENT_POWER_ON);
                } else if (current_state == STATE_LOCKED || current_state == STATE_UNLOCKED) {
                    // 开机状态下长按关机
                    ESP_LOGI(TAG, "Power OFF from %s", state_manager_get_state_name());
                    state_manager_handle_event(EVENT_POWER_OFF);
                }
            }
            break;

        case BUTTON_EVENT_LONG_PRESS_HOLD:
            ESP_LOGI(TAG, "Button %d long press hold", button_id);
            // 长按保持状态
            break;

        case BUTTON_EVENT_PRESS_REPEAT:
            ESP_LOGI(TAG, "Button %d press repeat", button_id);
            break;

        default:
            break;
    }
}

// 获取按钮状态
button_state_t get_button_state(button_id_t button_id) {
    if (button_id >= 0 && button_id < BUTTON_MAX) {
        return button_state[button_id];
    }
    return BUTTON_STATE_IDLE;
}

// 清除按钮状态
void clear_button_state(button_id_t button_id) {
    if (button_id >= 0 && button_id < BUTTON_MAX) {
        button_state[button_id] = BUTTON_STATE_IDLE;
    }
}

// 检查按钮是否被单击
bool is_button_clicked(button_id_t button_id) {
    if (button_id >= 0 && button_id < BUTTON_MAX) {
        return (button_state[button_id] == BUTTON_STATE_CLICKED);
    }
    return false;
}

// 检查按钮是否被双击
bool is_button_double_clicked(button_id_t button_id) {
    if (button_id >= 0 && button_id < BUTTON_MAX) {
        return (button_state[button_id] == BUTTON_STATE_DOUBLE_CLICKED);
    }
    return false;
}

// 检查按钮是否被长按
bool is_button_long_pressed(button_id_t button_id) {
    if (button_id >= 0 && button_id < BUTTON_MAX) {
        return (button_state[button_id] == BUTTON_STATE_LONG_PRESSED);
    }
    return false;
}

void button_task_init(void) {
    // 初始化所有按钮状态
    for (int i = 0; i < BUTTON_MAX; i++) {
        button_state[i] = BUTTON_STATE_IDLE;
    }

    // 初始化按钮驱动
    button_driver_init();
    
    // 注册按钮事件回调
    for (int i = 0; i < BUTTON_MAX; i++) {
        // 注册所有事件类型
        for (int event = 0; event < BUTTON_EVENT_MAX; event++) {
            button_register_callback(i, event, button_event_handler, NULL);
        }
    }
    
    ESP_LOGI(TAG, "Button task initialized");
}