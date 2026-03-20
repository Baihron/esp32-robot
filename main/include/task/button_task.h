#ifndef BUTTON_TASK_H
#define BUTTON_TASK_H

#include "button_driver.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 按钮状态定义
typedef enum {
    BUTTON_STATE_IDLE = 0,
    BUTTON_STATE_PRESSED,
    BUTTON_STATE_CLICKED,
    BUTTON_STATE_DOUBLE_CLICKED,
    BUTTON_STATE_LONG_PRESSED
} button_state_t;

// 初始化按钮任务
void button_task_init(void);

// 获取按钮状态
button_state_t get_button_state(button_id_t button_id);

// 清除按钮状态
void clear_button_state(button_id_t button_id);

// 检查按钮是否被单击
bool is_button_clicked(button_id_t button_id);

// 检查按钮是否被双击
bool is_button_double_clicked(button_id_t button_id);

// 检查按钮是否被长按
bool is_button_long_pressed(button_id_t button_id);

#ifdef __cplusplus
}
#endif

#endif // BUTTON_TASK_H