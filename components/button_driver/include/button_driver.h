#ifndef BUTTON_DRIVER_H
#define BUTTON_DRIVER_H

#include <stdint.h>
#include "driver/gpio.h"

// 按钮定义
typedef enum {
    BUTTON_BOOT = 0,    // GPIO0, BOOT按钮
    // BUTTON_USER = 1,    // 用户自定义按钮（例如GPIO21）
    BUTTON_MAX
} button_id_t;

// 按钮事件类型
typedef enum {
    BUTTON_EVENT_PRESS_DOWN = 0,
    BUTTON_EVENT_PRESS_UP,
    BUTTON_EVENT_PRESS_REPEAT,
    BUTTON_EVENT_SINGLE_CLICK,
    BUTTON_EVENT_DOUBLE_CLICK,
    BUTTON_EVENT_LONG_PRESS_START,
    BUTTON_EVENT_LONG_PRESS_HOLD,
    BUTTON_EVENT_MAX
} button_event_t;

// 按钮回调函数类型
typedef void (*button_callback_t)(button_id_t button_id, button_event_t event, void* user_data);

// 初始化按钮驱动
void button_driver_init(void);

// 注册按钮事件回调
void button_register_callback(button_id_t button_id, button_event_t event, 
                              button_callback_t callback, void* user_data);

// 获取按钮状态
int button_get_level(button_id_t button_id);

#endif // BUTTON_DRIVER_H