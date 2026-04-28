#include "emotion_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>
#include "eye_tracking.h"
#include "config.h"

static const char *TAG = "EMOTION_SYSTEM";

// 表情配置表
static const emotion_config_t emotion_configs[] = {
    {EMOTION_HAPPY,     "Happy",       0, 4000, true},
    {EMOTION_SAD,       "Sad",         0, 5000, false},
    {EMOTION_ANGRY,     "Angry",       0, 6000, false},
    {EMOTION_SURPRISED, "Surprised",   0, 2000, true},
    {EMOTION_SLEEPY,    "Sleepy",      0, 1000, true},
    {EMOTION_LOVING,    "Loving",      0, 3500, true},
    {EMOTION_CONFUSED,  "Confused",    0, 2500, true},
    {EMOTION_BLINKING,  "Blinking",    500,    0, false},
    {EMOTION_LAUGHING,  "Laughing",    0, 2000, true},
    {EMOTION_NEUTRAL,   "Neutral",     0, 3000, false}
};

// 表情系统状态
typedef struct {
    emotion_type_t current_emotion;
    emotion_type_t target_emotion;
    animation_state_t animation_state;
    emotion_context_t context;
    uint32_t animation_timer;
    uint32_t blink_timer;
    uint32_t emotion_start_time;
    bool is_blinking;
    uint8_t blink_frame;
} emotion_system_t;

static emotion_system_t g_emotion = {
    .current_emotion = EMOTION_NEUTRAL,
    .target_emotion = EMOTION_NEUTRAL,
    .animation_state = ANIMATION_IDLE,
    .context = {
        .x = 120,           // 默认中心X（240/2）
        .y = 120,           // 默认中心Y（240/2）
        .size = 100,         // 表情大小
        .color = 0x0000,    // 黑色线条
        .bg_color = 0xFFFF, // 白色背景
        .line_width = 5     // 线条宽度
    },
    .animation_timer = 0,
    .blink_timer = 0,
    .emotion_start_time = 0,
    .is_blinking = false,
    .blink_frame = 0
};

// 绘制基本图形函数
static void draw_circle(uint16_t* framebuffer, uint16_t width, uint16_t height,
                       int16_t cx, int16_t cy, int16_t radius, uint16_t color, bool fill) {
    int16_t x = radius;
    int16_t y = 0;
    int16_t err = 0;
    
    while (x >= y) {
        // 绘制8个对称点
        for (int i = 0; i < 8; i++) {
            int16_t px, py;
            switch (i) {
                case 0: px = cx + x; py = cy + y; break;
                case 1: px = cx + y; py = cy + x; break;
                case 2: px = cx - y; py = cy + x; break;
                case 3: px = cx - x; py = cy + y; break;
                case 4: px = cx - x; py = cy - y; break;
                case 5: px = cx - y; py = cy - x; break;
                case 6: px = cx + y; py = cy - x; break;
                case 7: px = cx + x; py = cy - y; break;
            }
            
            if (px >= 0 && px < width && py >= 0 && py < height) {
                if (fill) {
                    // 填充圆（简单实现，绘制水平线）
                    for (int16_t dx = -x; dx <= x; dx++) {
                        int16_t fill_px = cx + dx;
                        if (fill_px >= 0 && fill_px < width) {
                            int16_t dy = (int16_t)sqrt(radius * radius - dx * dx);
                            for (int16_t y_offset = -dy; y_offset <= dy; y_offset++) {
                                int16_t fill_py = cy + y_offset;
                                if (fill_py >= 0 && fill_py < height) {
                                    framebuffer[fill_py * width + fill_px] = color;
                                }
                            }
                        }
                    }
                } else {
                    framebuffer[py * width + px] = color;
                }
            }
        }
        
        if (err <= 0) {
            y += 1;
            err += 2*y + 1;
        }
        if (err > 0) {
            x -= 1;
            err -= 2*x + 1;
        }
    }
}

static void draw_line(uint16_t* framebuffer, uint16_t width, uint16_t height,
                     int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color, uint8_t width_px) {
    int16_t dx = abs(x2 - x1);
    int16_t dy = abs(y2 - y1);
    int16_t sx = (x1 < x2) ? 1 : -1;
    int16_t sy = (y1 < y2) ? 1 : -1;
    int16_t err = dx - dy;
    
    while (1) {
        // 绘制线条宽度
        for (int8_t w = -width_px/2; w <= width_px/2; w++) {
            for (int8_t h = -width_px/2; h <= width_px/2; h++) {
                int16_t px = x1 + w;
                int16_t py = y1 + h;
                if (px >= 0 && px < width && py >= 0 && py < height) {
                    framebuffer[py * width + px] = color;
                }
            }
        }
        
        if (x1 == x2 && y1 == y2) break;
        int16_t e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

static void draw_arc(uint16_t* framebuffer, uint16_t width, uint16_t height,
                    int16_t cx, int16_t cy, int16_t radius, 
                    float start_angle, float end_angle, uint16_t color, uint8_t width_px) {
    const int steps = 50;
    float angle_step = (end_angle - start_angle) / steps;
    
    for (int i = 0; i <= steps; i++) {
        float angle = start_angle + i * angle_step;
        int16_t x = cx + radius * cos(angle);
        int16_t y = cy + radius * sin(angle);
        
        // 绘制点（带宽度）
        for (int8_t w = -width_px/2; w <= width_px/2; w++) {
            for (int8_t h = -width_px/2; h <= width_px/2; h++) {
                int16_t px = x + w;
                int16_t py = y + h;
                if (px >= 0 && px < width && py >= 0 && py < height) {
                    framebuffer[py * width + px] = color;
                }
            }
        }
    }
}

// 修改眼睛绘制函数，添加视线偏移
static void draw_eyes_with_tracking(uint16_t* framebuffer, uint16_t width, uint16_t height,
                                   int16_t base_eye_x, int16_t eye_y, 
                                   int16_t eye_radius, int16_t eye_spacing,
                                   uint16_t color, uint8_t line_width, 
                                   bool blinking) {
    float offset_x, offset_y;
    eye_tracking_get_offset(&offset_x, &offset_y);
    
    // 放大偏移效果，让眼睛移动更明显
#ifdef CONFIG_EYE_TRACKING_INVERT_X
    float final_offset_x = (offset_x + 20) * 1.0f;
#else
    float final_offset_x = (offset_x - 20) * 1.0f;
#endif

    float final_offset_y = offset_y * 1.0f;

    // 打印当前偏移
    // ESP_LOGI(TAG, "Eye tracking - Original offset: (%.1f, %.1f), Final offset: (%.1f, %.1f)", offset_x, offset_y, final_offset_x, final_offset_y);

    // 左眼位置（考虑偏移）
    int16_t left_eye_x = base_eye_x - eye_spacing/2 + (int16_t)final_offset_x;
    int16_t left_eye_y = eye_y + (int16_t)final_offset_y;
    
    // 右眼位置（考虑偏移）- 注意：两只眼睛应该向同一方向移动
    int16_t right_eye_x = base_eye_x + eye_spacing/2 + (int16_t)final_offset_x;
    int16_t right_eye_y = eye_y + (int16_t)final_offset_y;
    
    // 打印眼睛位置
    // ESP_LOGI(TAG, "Eye positions - Left: (%d, %d), Right: (%d, %d)", left_eye_x, left_eye_y, right_eye_x, right_eye_y);
    
    if (blinking) {
        // 眨眼效果
        draw_line(framebuffer, width, height, 
                 left_eye_x - eye_radius, left_eye_y,
                 left_eye_x + eye_radius, left_eye_y,
                 color, line_width);
        draw_line(framebuffer, width, height,
                 right_eye_x - eye_radius, right_eye_y,
                 right_eye_x + eye_radius, right_eye_y,
                 color, line_width);
    } else {
        // 正常眼睛
        draw_circle(framebuffer, width, height, 
                   left_eye_x, left_eye_y, eye_radius, color, true);
        draw_circle(framebuffer, width, height,
                   right_eye_x, right_eye_y, eye_radius, color, true);
    }
}

// 绘制各种表情
static void draw_neutral_face(uint16_t* framebuffer, uint16_t width, uint16_t height, 
                             emotion_context_t* ctx, bool blinking) {
    // 眼睛
    int16_t eye_y = ctx->y - 40;        // 眼睛Y位置
    int16_t eye_width = 30;             // 眼睛横线宽度
    int16_t eye_spacing = 80;           // 眼睛间距
    
    // 左眼横线
    draw_line(framebuffer, width, height,
             ctx->x - eye_spacing/2 - eye_width/2, eye_y,
             ctx->x - eye_spacing/2 + eye_width/2, eye_y,
             ctx->color, ctx->line_width);

    // 右眼横线
    draw_line(framebuffer, width, height,
             ctx->x + eye_spacing/2 - eye_width/2, eye_y,
             ctx->x + eye_spacing/2 + eye_width/2, eye_y,
             ctx->color, ctx->line_width);

    // 嘴巴 - 直线（加长加粗）
    int16_t mouth_y = ctx->y + 40;  // 嘴巴Y位置
    int16_t mouth_width = 100;      // 嘴巴宽度
    
    draw_line(framebuffer, width, height,
             ctx->x - mouth_width/2, mouth_y,
             ctx->x + mouth_width/2, mouth_y,
             ctx->color, ctx->line_width);
}

static void draw_happy_face(uint16_t* framebuffer, uint16_t width, uint16_t height,
                           emotion_context_t* ctx, bool blinking) {
    // 眼睛
    int16_t eye_y = ctx->y - 40;
    int16_t eye_radius = 20;
    int16_t eye_spacing = 80;
    
    draw_eyes_with_tracking(framebuffer, width, height,
                           ctx->x, eye_y, eye_radius, eye_spacing,
                           ctx->color, ctx->line_width, blinking);
    
    // 微笑的嘴巴（弧线）
    int16_t mouth_y = ctx->y + 20;
    int16_t mouth_radius = 60;
    
    draw_arc(framebuffer, width, height,
            ctx->x, mouth_y, mouth_radius,
            M_PI * 0.2, M_PI * 0.8, ctx->color, ctx->line_width);
}

static void draw_sad_face(uint16_t* framebuffer, uint16_t width, uint16_t height,
                         emotion_context_t* ctx, bool blinking) {
    int16_t eye_y = ctx->y - 40;
    int16_t eye_radius = 18;
    int16_t eye_spacing = 80;
    
    draw_eyes_with_tracking(framebuffer, width, height,
                           ctx->x, eye_y, eye_radius, eye_spacing,
                           ctx->color, ctx->line_width, blinking);
    
    // 悲伤的嘴巴（大倒弧线）
    int16_t mouth_y = ctx->y + 60;
    int16_t mouth_radius = 50;
    
    draw_arc(framebuffer, width, height,
            ctx->x, mouth_y, mouth_radius,
            M_PI * 1.2, M_PI * 1.8, ctx->color, ctx->line_width);
}

// 修改生气表情
static void draw_angry_face(uint16_t* framebuffer, uint16_t width, uint16_t height,
                           emotion_context_t* ctx, bool blinking) {
    // 使用带视线追踪的眼睛绘制（皱眉效果通过偏移来实现）
    int16_t eye_y = ctx->y - 40;
    int16_t eye_radius = 20;
    int16_t eye_spacing = 80;
    
    // 先用带追踪的眼睛绘制
    draw_eyes_with_tracking(framebuffer, width, height,
                           ctx->x, eye_y, eye_radius, eye_spacing,
                           ctx->color, ctx->line_width, blinking);
    
    // 在眼睛上方添加皱眉的眉毛（斜线）
    float offset_x, offset_y;
    eye_tracking_get_offset(&offset_x, &offset_y);
    
    int16_t left_brow_x = ctx->x - eye_spacing/2 - 10 + (int16_t)(offset_x * 1.5f);
    int16_t right_brow_x = ctx->x + eye_spacing/2 - 10 + (int16_t)(offset_x * 1.5f);
    int16_t brow_y = eye_y - 25 + (int16_t)(offset_y * 1.0f);
    
    // 左眉（向下倾斜）
    draw_line(framebuffer, width, height,
             left_brow_x - 10, brow_y - 5,
             left_brow_x + 30, brow_y,
             ctx->color, ctx->line_width);
    // 右眉（向下倾斜）
    draw_line(framebuffer, width, height,
             right_brow_x - 10, brow_y - 5,
             right_brow_x + 30, brow_y,
             ctx->color, ctx->line_width);
    
    // 生气的嘴巴（大倒V形）
    int16_t mouth_y = ctx->y + 40;
    
    draw_line(framebuffer, width, height,
             ctx->x - 50, mouth_y,
             ctx->x, mouth_y + 30,
             ctx->color, ctx->line_width);
    draw_line(framebuffer, width, height,
             ctx->x, mouth_y + 30,
             ctx->x + 50, mouth_y,
             ctx->color, ctx->line_width);
}

// 修改惊讶表情 - 使用带追踪的眼睛
static void draw_surprised_face(uint16_t* framebuffer, uint16_t width, uint16_t height,
                               emotion_context_t* ctx, bool blinking) {
    // 使用带视线追踪的大眼睛
    int16_t eye_y = ctx->y - 40;
    int16_t eye_radius = 25;  // 更大的眼睛
    int16_t eye_spacing = 80;
    
    draw_eyes_with_tracking(framebuffer, width, height,
                           ctx->x, eye_y, eye_radius, eye_spacing,
                           ctx->color, ctx->line_width, blinking);
    
    // O形大嘴巴
    int16_t mouth_y = ctx->y + 40;
    int16_t mouth_radius = 30;
    
    draw_circle(framebuffer, width, height,
               ctx->x, mouth_y, mouth_radius, ctx->color, false);
}

// 修改困倦表情 - 使用 draw_eyes_with_tracking 相同的偏移计算逻辑
static void draw_sleepy_face(uint16_t* framebuffer, uint16_t width, uint16_t height,
                            emotion_context_t* ctx, bool blinking) {
    // 半闭的眼睛（弧线）
    int16_t eye_y = ctx->y - 40;
    int16_t eye_radius = 15;
    int16_t eye_spacing = 80;
    
    // 使用与 draw_eyes_with_tracking 完全相同的偏移计算逻辑
    float offset_x, offset_y;
    eye_tracking_get_offset(&offset_x, &offset_y);
    
#ifdef CONFIG_EYE_TRACKING_INVERT_X
    float final_offset_x = (offset_x + 20) * 1.0f;
#else
    float final_offset_x = (offset_x - 20) * 1.0f;
#endif
    float final_offset_y = offset_y * 1.0f;
    
    // 左眼位置（考虑偏移）
    int16_t left_eye_x = ctx->x - eye_spacing/2 + (int16_t)final_offset_x;
    int16_t left_eye_y = eye_y + (int16_t)final_offset_y;
    
    // 右眼位置（考虑偏移）
    int16_t right_eye_x = ctx->x + eye_spacing/2 + (int16_t)final_offset_x;
    int16_t right_eye_y = eye_y + (int16_t)final_offset_y;
    
    if (blinking) {
        // 眨眼时画线
        draw_line(framebuffer, width, height,
                 left_eye_x - eye_radius, left_eye_y,
                 left_eye_x + eye_radius, left_eye_y,
                 ctx->color, ctx->line_width);
        draw_line(framebuffer, width, height,
                 right_eye_x - eye_radius, right_eye_y,
                 right_eye_x + eye_radius, right_eye_y,
                 ctx->color, ctx->line_width);
    } else {
        // 半闭的眼睛（弧线）
        draw_arc(framebuffer, width, height,
                left_eye_x, left_eye_y, eye_radius,
                M_PI * 0.1, M_PI * 0.9, ctx->color, ctx->line_width);
        draw_arc(framebuffer, width, height,
                right_eye_x, right_eye_y, eye_radius,
                M_PI * 0.1, M_PI * 0.9, ctx->color, ctx->line_width);
    }
    
    // Zzz嘴巴（波浪线）
    int16_t mouth_y = ctx->y + 40;
    
    // 绘制Z形
    draw_line(framebuffer, width, height,
             ctx->x - 40, mouth_y,
             ctx->x + 40, mouth_y,
             ctx->color, ctx->line_width);
    draw_line(framebuffer, width, height,
             ctx->x + 40, mouth_y,
             ctx->x - 40, mouth_y + 20,
             ctx->color, ctx->line_width);
    draw_line(framebuffer, width, height,
             ctx->x - 40, mouth_y + 20,
             ctx->x + 40, mouth_y + 20,
             ctx->color, ctx->line_width);
}

// 修改喜爱表情 - 使用 draw_eyes_with_tracking 相同的偏移计算逻辑
static void draw_loving_face(uint16_t* framebuffer, uint16_t width, uint16_t height,
                            emotion_context_t* ctx, bool blinking) {
    // 心形眼睛
    int16_t eye_y = ctx->y - 40;
    int16_t eye_radius = 15;
    int16_t eye_spacing = 80;
    
    // 使用与 draw_eyes_with_tracking 完全相同的偏移计算逻辑
    float offset_x, offset_y;
    eye_tracking_get_offset(&offset_x, &offset_y);
    
#ifdef CONFIG_EYE_TRACKING_INVERT_X
    float final_offset_x = (offset_x + 20) * 1.0f;
#else
    float final_offset_x = (offset_x - 20) * 1.0f;
#endif
    float final_offset_y = offset_y * 1.0f;
    
    // 左眼位置（考虑偏移）
    int16_t left_eye_x = ctx->x - eye_spacing/2 + (int16_t)final_offset_x;
    int16_t left_eye_y = eye_y + (int16_t)final_offset_y;
    
    // 右眼位置（考虑偏移）
    int16_t right_eye_x = ctx->x + eye_spacing/2 + (int16_t)final_offset_x;
    int16_t right_eye_y = eye_y + (int16_t)final_offset_y;
    
    if (blinking) {
        // 眨眼时画线
        draw_line(framebuffer, width, height,
                 left_eye_x - eye_radius, left_eye_y,
                 left_eye_x + eye_radius, left_eye_y,
                 ctx->color, ctx->line_width);
        draw_line(framebuffer, width, height,
                 right_eye_x - eye_radius, right_eye_y,
                 right_eye_x + eye_radius, right_eye_y,
                 ctx->color, ctx->line_width);
    } else {
        // 左心形
        draw_arc(framebuffer, width, height,
                left_eye_x - 10, left_eye_y, 15,
                M_PI * 0.5, M_PI * 1.5, ctx->color, ctx->line_width);
        draw_arc(framebuffer, width, height,
                left_eye_x + 10, left_eye_y, 15,
                M_PI * 1.5, M_PI * 2.5, ctx->color, ctx->line_width);
        
        // 右心形
        draw_arc(framebuffer, width, height,
                right_eye_x - 10, right_eye_y, 15,
                M_PI * 0.5, M_PI * 1.5, ctx->color, ctx->line_width);
        draw_arc(framebuffer, width, height,
                right_eye_x + 10, right_eye_y, 15,
                M_PI * 1.5, M_PI * 2.5, ctx->color, ctx->line_width);
    }
    
    // 微笑的嘴巴
    int16_t mouth_y = ctx->y + 40;
    int16_t mouth_radius = 40;
    
    draw_arc(framebuffer, width, height,
            ctx->x, mouth_y, mouth_radius,
            M_PI * 0.3, M_PI * 0.7, ctx->color, ctx->line_width);
}

// 修改困惑表情 - 使用 draw_eyes_with_tracking 相同的偏移计算逻辑
static void draw_confused_face(uint16_t* framebuffer, uint16_t width, uint16_t height,
                              emotion_context_t* ctx, bool blinking) {
    // 不对称的眼睛
    int16_t eye_y = ctx->y - 40;
    int16_t eye_spacing = 80;
    
    // 使用与 draw_eyes_with_tracking 完全相同的偏移计算逻辑
    float offset_x, offset_y;
    eye_tracking_get_offset(&offset_x, &offset_y);
    
#ifdef CONFIG_EYE_TRACKING_INVERT_X
    float final_offset_x = (offset_x + 20) * 1.0f;
#else
    float final_offset_x = (offset_x - 20) * 1.0f;
#endif
    float final_offset_y = offset_y * 1.0f;
    
    // 左眼位置（考虑偏移）
    int16_t left_eye_x = ctx->x - eye_spacing/2 + (int16_t)final_offset_x;
    int16_t left_eye_y = eye_y + (int16_t)final_offset_y;

    // 右眼位置（考虑偏移）
    int16_t right_eye_x = ctx->x + eye_spacing/2 + (int16_t)final_offset_x;
    int16_t right_eye_y = eye_y + (int16_t)final_offset_y;
    
    if (blinking) {
        // 左眼眨眼
        draw_line(framebuffer, width, height,
                 left_eye_x - 15, left_eye_y,
                 left_eye_x + 15, left_eye_y,
                 ctx->color, ctx->line_width);
        // 右眼眨眼
        draw_line(framebuffer, width, height,
                 right_eye_x - 15, right_eye_y,
                 right_eye_x + 15, right_eye_y,
                 ctx->color, ctx->line_width);
    } else {
        // 左眼（正常圆）
        draw_circle(framebuffer, width, height,
                   left_eye_x, left_eye_y, 20, ctx->color, true);
        
        // 右眼（斜线）
        draw_line(framebuffer, width, height,
                 right_eye_x - 15, right_eye_y - 10,
                 right_eye_x + 15, right_eye_y + 10,
                 ctx->color, ctx->line_width);
    }
    
    // 问号嘴巴
    int16_t mouth_y = ctx->y + 40;
    
    // 绘制问号形状
    draw_arc(framebuffer, width, height,
            ctx->x, mouth_y - 10, 15,
            M_PI * 0.2, M_PI * 1.8, ctx->color, ctx->line_width);
    draw_line(framebuffer, width, height,
             ctx->x, mouth_y + 5,
             ctx->x, mouth_y + 20,
             ctx->color, ctx->line_width);
    draw_circle(framebuffer, width, height,
               ctx->x, mouth_y + 25, 3, ctx->color, true);
}

// 修改大笑表情 - 使用 draw_eyes_with_tracking 相同的偏移计算逻辑
static void draw_laughing_face(uint16_t* framebuffer, uint16_t width, uint16_t height,
                              emotion_context_t* ctx, bool blinking) {
    // 眯起的眼睛（弧线）
    int16_t eye_y = ctx->y - 40;
    int16_t eye_radius = 12;
    int16_t eye_spacing = 80;
    
    // 使用与 draw_eyes_with_tracking 完全相同的偏移计算逻辑
    float offset_x, offset_y;
    eye_tracking_get_offset(&offset_x, &offset_y);
    
#ifdef CONFIG_EYE_TRACKING_INVERT_X
    float final_offset_x = (offset_x + 20) * 1.0f;
#else
    float final_offset_x = (offset_x - 20) * 1.0f;
#endif
    float final_offset_y = offset_y * 1.0f;
    
    // 左眼位置（考虑偏移）
    int16_t left_eye_x = ctx->x - eye_spacing/2 + (int16_t)final_offset_x;
    int16_t left_eye_y = eye_y + (int16_t)final_offset_y;
    
    // 右眼位置（考虑偏移）
    int16_t right_eye_x = ctx->x + eye_spacing/2 + (int16_t)final_offset_x;
    int16_t right_eye_y = eye_y + (int16_t)final_offset_y;
    
    if (blinking) {
        // 眨眼时画线
        draw_line(framebuffer, width, height,
                 left_eye_x - eye_radius, left_eye_y,
                 left_eye_x + eye_radius, left_eye_y,
                 ctx->color, ctx->line_width);
        draw_line(framebuffer, width, height,
                 right_eye_x - eye_radius, right_eye_y,
                 right_eye_x + eye_radius, right_eye_y,
                 ctx->color, ctx->line_width);
    } else {
        // 眯起的眼睛（弧线）
        draw_arc(framebuffer, width, height,
                left_eye_x, left_eye_y, eye_radius,
                M_PI * 0.1, M_PI * 0.9, ctx->color, ctx->line_width);
        draw_arc(framebuffer, width, height,
                right_eye_x, right_eye_y, eye_radius,
                M_PI * 0.1, M_PI * 0.9, ctx->color, ctx->line_width);
    }
    
    // 大笑的嘴巴（大弧线）
    int16_t mouth_y = ctx->y + 30;
    int16_t mouth_radius = 50;
    
    draw_arc(framebuffer, width, height,
            ctx->x, mouth_y, mouth_radius,
            M_PI * 0.1, M_PI * 0.9, ctx->color, ctx->line_width);
    
    // 添加舌头效果
    draw_arc(framebuffer, width, height,
            ctx->x, mouth_y + 10, 20,
            M_PI * 0.3, M_PI * 0.7, ctx->color, ctx->line_width);
}

// 表情系统初始化
esp_err_t emotion_system_init(void) {
    ESP_LOGI(TAG, "Emotion system initializing...");
    
    // 初始化默认表情
    g_emotion.current_emotion = EMOTION_NEUTRAL;
    g_emotion.target_emotion = EMOTION_NEUTRAL;
    g_emotion.animation_state = ANIMATION_IDLE;
    g_emotion.emotion_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    g_emotion.blink_timer = 0;
    g_emotion.is_blinking = false;
    
    ESP_LOGI(TAG, "Emotion system initialized with %d emotions", EMOTION_COUNT);
    return ESP_OK;
}

// 设置当前表情
esp_err_t emotion_set_current(emotion_type_t emotion) {
    if (emotion >= EMOTION_COUNT) {
        ESP_LOGE(TAG, "Invalid emotion type: %d", emotion);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Setting emotion to: %s", emotion_configs[emotion].name);

    g_emotion.target_emotion = emotion;
    g_emotion.animation_state = ANIMATION_TRANSITION;
    g_emotion.animation_timer = 0;
    g_emotion.emotion_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    // 如果是眨眼表情，设置较短的持续时间
    if (emotion == EMOTION_BLINKING) {
        // 眨眼表情会自动恢复，所以设置开始时间
        g_emotion.emotion_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    }

    return ESP_OK;
}

// 获取当前表情
emotion_type_t emotion_get_current(void) {
    return g_emotion.current_emotion;
}

// 设置表情动画状态
void emotion_set_animation(animation_state_t state) {
    g_emotion.animation_state = state;
    if (state == ANIMATION_BLINK) {
        g_emotion.is_blinking = true;
        g_emotion.blink_frame = 0;
    }
}

// 绘制表情到帧缓冲区
void emotion_draw_to_buffer(uint16_t* framebuffer, uint16_t width, uint16_t height) {

    bool blinking = g_emotion.is_blinking && (g_emotion.blink_frame >= 2 && g_emotion.blink_frame <= 3);

    // 根据当前表情类型调用相应的绘制函数
    switch (g_emotion.current_emotion) {
        case EMOTION_NEUTRAL:
            draw_neutral_face(framebuffer, width, height, &g_emotion.context, blinking);
            break;

        case EMOTION_HAPPY:
            draw_happy_face(framebuffer, width, height, &g_emotion.context, blinking);
            break;

        case EMOTION_SAD:
            draw_sad_face(framebuffer, width, height, &g_emotion.context, blinking);
            break;

        case EMOTION_ANGRY:
            draw_angry_face(framebuffer, width, height, &g_emotion.context, blinking);
            break;

        case EMOTION_SURPRISED:
            draw_surprised_face(framebuffer, width, height, &g_emotion.context, blinking);
            break;

        case EMOTION_SLEEPY:
            draw_sleepy_face(framebuffer, width, height, &g_emotion.context, blinking);
            break;

        case EMOTION_LOVING:
            draw_loving_face(framebuffer, width, height, &g_emotion.context, blinking);
            break;

        case EMOTION_CONFUSED:
            draw_confused_face(framebuffer, width, height, &g_emotion.context, blinking);
            break;

        case EMOTION_BLINKING:
            // 专门用于眨眼的动画状态
            switch (g_emotion.target_emotion) {
                case EMOTION_HAPPY:
                    draw_happy_face(framebuffer, width, height, &g_emotion.context, true);
                    break;
                case EMOTION_SAD:
                    draw_sad_face(framebuffer, width, height, &g_emotion.context, true);
                    break;
                default:
                    draw_happy_face(framebuffer, width, height, &g_emotion.context, true);
                    break;
            }
            break;

        default:
            // 默认绘制中性表情
            draw_happy_face(framebuffer, width, height, &g_emotion.context, blinking);
            break;
    }
}

// 更新表情动画
void emotion_update_animation(uint32_t elapsed_ms) {
    // 更新动画计时器
    g_emotion.animation_timer += elapsed_ms;
    
    // 处理表情切换过渡
    if (g_emotion.animation_state == ANIMATION_TRANSITION) {
        if (g_emotion.animation_timer >= 500) { // 500ms过渡时间
            g_emotion.current_emotion = g_emotion.target_emotion;
            g_emotion.animation_state = ANIMATION_IDLE;
            g_emotion.animation_timer = 0;
            ESP_LOGI(TAG, "Emotion transition complete to: %s", 
                    emotion_configs[g_emotion.current_emotion].name);
        }
    }
    
    // 处理眨眼动画
    if (g_emotion.is_blinking) {
        g_emotion.blink_frame++;
        if (g_emotion.blink_frame >= 4) { // 眨眼持续6帧
            g_emotion.is_blinking = false;
            g_emotion.blink_frame = 0;
        }
    } else {
        // 检查是否需要自动眨眼
        const emotion_config_t* config = &emotion_configs[g_emotion.current_emotion];
        if (config->can_blink && config->blink_interval > 0) {
            g_emotion.blink_timer += elapsed_ms;
            // 缩短眨眼间隔（如果支持眨眼）
            uint32_t actual_interval = config->blink_interval;
            if (actual_interval > 2000) {
                actual_interval = 2000;  // 最大间隔2秒
            }
            if (g_emotion.blink_timer >= actual_interval) {
                g_emotion.is_blinking = true;
                g_emotion.blink_frame = 0;
                g_emotion.blink_timer = 0;
            }
        }
    }
    
    // 检查表情是否需要自动切换（如果有持续时间）
    const emotion_config_t* config = &emotion_configs[g_emotion.current_emotion];
    if (config->duration_ms > 0 && g_emotion.current_emotion == EMOTION_BLINKING) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (current_time - g_emotion.emotion_start_time >= config->duration_ms) {
            // 只有眨眼表情会自动切换回中性
            emotion_set_current(EMOTION_NEUTRAL);
            ESP_LOGI(TAG, "Blinking animation complete, returning to neutral");
        }
    }
}

// 表情系统反初始化
void emotion_system_deinit(void) {
    ESP_LOGI(TAG, "Emotion system deinitializing...");
    // 清理资源
    memset(&g_emotion, 0, sizeof(g_emotion));
}

// 获取表情名称
const char* emotion_get_name(emotion_type_t emotion) {
    if (emotion < EMOTION_COUNT) {
        return emotion_configs[emotion].name;
    }
    return "UNKNOWN";
}

bool emotion_is_blinking(void) {
    return g_emotion.is_blinking;
}