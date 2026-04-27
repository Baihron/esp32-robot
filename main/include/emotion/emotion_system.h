#ifndef EMOTION_SYSTEM_H
#define EMOTION_SYSTEM_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

// 表情类型枚举
typedef enum {
    EMOTION_NEUTRAL = 0,      // 中性
    EMOTION_HAPPY,           // 开心
    EMOTION_SAD,             // 悲伤
    EMOTION_ANGRY,           // 生气
    EMOTION_SURPRISED,       // 惊讶
    EMOTION_SLEEPY,          // 困倦
    EMOTION_LOVING,          // 喜爱
    EMOTION_CONFUSED,        // 困惑
    EMOTION_BLINKING,        // 眨眼（动画）
    EMOTION_LAUGHING,        // 大笑
    EMOTION_COUNT            // 表情总数
} emotion_type_t;

// 表情动画状态
typedef enum {
    ANIMATION_IDLE = 0,      // 空闲
    ANIMATION_BLINK,         // 眨眼
    ANIMATION_TRANSITION,    // 过渡
    ANIMATION_LOOP           // 循环动画
} animation_state_t;

// 表情配置结构
typedef struct {
    emotion_type_t type;     // 表情类型
    const char* name;        // 表情名称
    uint32_t duration_ms;    // 显示时长（0表示无限）
    uint32_t blink_interval; // 眨眼间隔（毫秒）
    bool can_blink;          // 是否可以眨眼
} emotion_config_t;

// 表情绘制上下文
typedef struct {
    uint16_t x;             // 表情中心X坐标
    uint16_t y;             // 表情中心Y坐标
    uint16_t size;          // 表情大小（像素）
    uint16_t color;         // 线条颜色
    uint16_t bg_color;      // 背景颜色
    uint8_t line_width;     // 线条宽度
} emotion_context_t;

// 表情系统初始化
esp_err_t emotion_system_init(void);

// 设置当前表情
esp_err_t emotion_set_current(emotion_type_t emotion);

// 获取当前表情
emotion_type_t emotion_get_current(void);

// 设置表情动画状态
void emotion_set_animation(animation_state_t state);

// 绘制表情到帧缓冲区
void emotion_draw_to_buffer(uint16_t* framebuffer, uint16_t width, uint16_t height);

// 更新表情动画（需要周期性调用）
void emotion_update_animation(uint32_t elapsed_ms);

// 表情系统反初始化
void emotion_system_deinit(void);

// 获取表情名称
const char* emotion_get_name(emotion_type_t emotion);

#ifdef __cplusplus
}
#endif

#endif /* EMOTION_SYSTEM_H */