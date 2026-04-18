#include "emotion_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

static const char *TAG = "EMOTION_SYSTEM";

// 表情配置表
static const emotion_config_t emotion_configs[] = {
    // 中性表情
    {EMOTION_NEUTRAL, "Neutral", 0, 3000, true},
    // 开心表情
    {EMOTION_HAPPY, "Happy", 5000, 4000, true},
    // 悲伤表情
    {EMOTION_SAD, "Sad", 5000, 5000, false},
    // 生气表情
    {EMOTION_ANGRY, "Angry", 3000, 6000, false},
    // 惊讶表情
    {EMOTION_SURPRISED, "Surprised", 2000, 2000, true},
    // 困倦表情
    {EMOTION_SLEEPY, "Sleepy", 0, 1000, true},
    // 喜爱表情
    {EMOTION_LOVING, "Loving", 4000, 3500, true},
    // 困惑表情
    {EMOTION_CONFUSED, "Confused", 4000, 2500, true},
    // 眨眼动画
    {EMOTION_BLINKING, "Blinking", 500, 0, false},
    // 大笑
    {EMOTION_LAUGHING, "Laughing", 3000, 2000, true}
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
        .size = 80,         // 表情大小
        .color = 0x0000,    // 黑色线条
        .bg_color = 0xFFFF, // 白色背景
        .line_width = 3     // 线条宽度
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

// 绘制各种表情
static void draw_neutral_face(uint16_t* framebuffer, uint16_t width, uint16_t height, 
                             emotion_context_t* ctx, bool blinking) {
    // 绘制脸部轮廓
    draw_circle(framebuffer, width, height, ctx->x, ctx->y, ctx->size, ctx->color, false);
    
    // 眼睛
    int16_t eye_y = ctx->y - ctx->size / 4;
    int16_t eye_radius = ctx->size / 10;
    
    if (blinking) {
        // 眨眼效果 - 画一条线
        draw_line(framebuffer, width, height, 
                 ctx->x - ctx->size/3, eye_y,
                 ctx->x - ctx->size/3 + eye_radius*2, eye_y,
                 ctx->color, ctx->line_width);
        draw_line(framebuffer, width, height,
                 ctx->x + ctx->size/3 - eye_radius*2, eye_y,
                 ctx->x + ctx->size/3, eye_y,
                 ctx->color, ctx->line_width);
    } else {
        // 正常眼睛
        draw_circle(framebuffer, width, height, 
                   ctx->x - ctx->size/3, eye_y, eye_radius, ctx->color, true);
        draw_circle(framebuffer, width, height,
                   ctx->x + ctx->size/3, eye_y, eye_radius, ctx->color, true);
    }
    
    // 嘴巴 - 直线
    draw_line(framebuffer, width, height,
             ctx->x - ctx->size/3, ctx->y + ctx->size/4,
             ctx->x + ctx->size/3, ctx->y + ctx->size/4,
             ctx->color, ctx->line_width);
}

static void draw_happy_face(uint16_t* framebuffer, uint16_t width, uint16_t height,
                           emotion_context_t* ctx, bool blinking) {
    // 脸部轮廓
    draw_circle(framebuffer, width, height, ctx->x, ctx->y, ctx->size, ctx->color, false);
    
    // 眼睛
    int16_t eye_y = ctx->y - ctx->size / 4;
    int16_t eye_radius = ctx->size / 10;
    
    if (blinking) {
        draw_line(framebuffer, width, height,
                 ctx->x - ctx->size/3, eye_y,
                 ctx->x - ctx->size/3 + eye_radius*2, eye_y,
                 ctx->color, ctx->line_width);
        draw_line(framebuffer, width, height,
                 ctx->x + ctx->size/3 - eye_radius*2, eye_y,
                 ctx->x + ctx->size/3, eye_y,
                 ctx->color, ctx->line_width);
    } else {
        draw_circle(framebuffer, width, height,
                   ctx->x - ctx->size/3, eye_y, eye_radius, ctx->color, true);
        draw_circle(framebuffer, width, height,
                   ctx->x + ctx->size/3, eye_y, eye_radius, ctx->color, true);
    }
    
    // 微笑的嘴巴（弧线）
    draw_arc(framebuffer, width, height,
            ctx->x, ctx->y + ctx->size/6, ctx->size/3,
            M_PI * 0.2, M_PI * 0.8, ctx->color, ctx->line_width);
}

static void draw_sad_face(uint16_t* framebuffer, uint16_t width, uint16_t height,
                         emotion_context_t* ctx, bool blinking) {
    // 脸部轮廓
    draw_circle(framebuffer, width, height, ctx->x, ctx->y, ctx->size, ctx->color, false);
    
    // 眼睛（下垂）
    int16_t eye_y = ctx->y - ctx->size / 4;
    int16_t eye_radius = ctx->size / 12;
    
    if (blinking) {
        draw_line(framebuffer, width, height,
                 ctx->x - ctx->size/3, eye_y,
                 ctx->x - ctx->size/3 + eye_radius*2, eye_y,
                 ctx->color, ctx->line_width);
        draw_line(framebuffer, width, height,
                 ctx->x + ctx->size/3 - eye_radius*2, eye_y,
                 ctx->x + ctx->size/3, eye_y,
                 ctx->color, ctx->line_width);
    } else {
        draw_circle(framebuffer, width, height,
                   ctx->x - ctx->size/3, eye_y, eye_radius, ctx->color, true);
        draw_circle(framebuffer, width, height,
                   ctx->x + ctx->size/3, eye_y, eye_radius, ctx->color, true);
    }
    
    // 悲伤的嘴巴（倒弧线）
    draw_arc(framebuffer, width, height,
            ctx->x, ctx->y + ctx->size/3, ctx->size/3,
            M_PI * 1.2, M_PI * 1.8, ctx->color, ctx->line_width);
}

// 其他表情的绘制函数类似，这里省略部分实现...

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
    // 清空表情区域（可选，如果背景需要清除）
    // 这里我们假设调用者已经设置了背景
    
    bool blinking = g_emotion.is_blinking && (g_emotion.blink_frame < 3);
    
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
            // 绘制生气表情
            draw_circle(framebuffer, width, height, 
                       g_emotion.context.x, g_emotion.context.y, 
                       g_emotion.context.size, g_emotion.context.color, false);
            // 皱眉的眼睛
            draw_line(framebuffer, width, height,
                     g_emotion.context.x - g_emotion.context.size/3 - 5,
                     g_emotion.context.y - g_emotion.context.size/4 - 5,
                     g_emotion.context.x - g_emotion.context.size/3 + 15,
                     g_emotion.context.y - g_emotion.context.size/4,
                     g_emotion.context.color, g_emotion.context.line_width);
            draw_line(framebuffer, width, height,
                     g_emotion.context.x + g_emotion.context.size/3 - 15,
                     g_emotion.context.y - g_emotion.context.size/4 - 5,
                     g_emotion.context.x + g_emotion.context.size/3 + 5,
                     g_emotion.context.y - g_emotion.context.size/4,
                     g_emotion.context.color, g_emotion.context.line_width);
            // 生气的嘴巴（倒V形）
            draw_line(framebuffer, width, height,
                     g_emotion.context.x - g_emotion.context.size/4,
                     g_emotion.context.y + g_emotion.context.size/4,
                     g_emotion.context.x,
                     g_emotion.context.y + g_emotion.context.size/3,
                     g_emotion.context.color, g_emotion.context.line_width);
            draw_line(framebuffer, width, height,
                     g_emotion.context.x,
                     g_emotion.context.y + g_emotion.context.size/3,
                     g_emotion.context.x + g_emotion.context.size/4,
                     g_emotion.context.y + g_emotion.context.size/4,
                     g_emotion.context.color, g_emotion.context.line_width);
            break;
            
        case EMOTION_SURPRISED:
            // 绘制惊讶表情
            draw_circle(framebuffer, width, height,
                       g_emotion.context.x, g_emotion.context.y,
                       g_emotion.context.size, g_emotion.context.color, false);
            // 大眼睛
            draw_circle(framebuffer, width, height,
                       g_emotion.context.x - g_emotion.context.size/3,
                       g_emotion.context.y - g_emotion.context.size/4,
                       g_emotion.context.size/8, g_emotion.context.color, true);
            draw_circle(framebuffer, width, height,
                       g_emotion.context.x + g_emotion.context.size/3,
                       g_emotion.context.y - g_emotion.context.size/4,
                       g_emotion.context.size/8, g_emotion.context.color, true);
            // O形嘴巴
            draw_circle(framebuffer, width, height,
                       g_emotion.context.x,
                       g_emotion.context.y + g_emotion.context.size/4,
                       g_emotion.context.size/6, g_emotion.context.color, false);
            break;
            
        case EMOTION_SLEEPY:
            // 绘制困倦表情
            draw_circle(framebuffer, width, height,
                       g_emotion.context.x, g_emotion.context.y,
                       g_emotion.context.size, g_emotion.context.color, false);
            // 半闭的眼睛
            draw_arc(framebuffer, width, height,
                    g_emotion.context.x - g_emotion.context.size/3,
                    g_emotion.context.y - g_emotion.context.size/4,
                    g_emotion.context.size/12,
                    M_PI * 0.1, M_PI * 0.9, g_emotion.context.color, g_emotion.context.line_width);
            draw_arc(framebuffer, width, height,
                    g_emotion.context.x + g_emotion.context.size/3,
                    g_emotion.context.y - g_emotion.context.size/4,
                    g_emotion.context.size/12,
                    M_PI * 0.1, M_PI * 0.9, g_emotion.context.color, g_emotion.context.line_width);
            // Zzz嘴巴
            draw_line(framebuffer, width, height,
                     g_emotion.context.x - g_emotion.context.size/4,
                     g_emotion.context.y + g_emotion.context.size/4,
                     g_emotion.context.x + g_emotion.context.size/4,
                     g_emotion.context.y + g_emotion.context.size/4,
                     g_emotion.context.color, g_emotion.context.line_width);
            break;
            
        case EMOTION_BLINKING:
            // 专门用于眨眼的动画状态
            if (g_emotion.blink_frame == 0) {
                draw_neutral_face(framebuffer, width, height, &g_emotion.context, false);
            } else {
                draw_neutral_face(framebuffer, width, height, &g_emotion.context, true);
            }
            break;
            
        default:
            // 默认绘制中性表情
            draw_neutral_face(framebuffer, width, height, &g_emotion.context, blinking);
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
        if (g_emotion.blink_frame >= 6) { // 眨眼持续6帧
            g_emotion.is_blinking = false;
            g_emotion.blink_frame = 0;
        }
    } else {
        // 检查是否需要自动眨眼
        const emotion_config_t* config = &emotion_configs[g_emotion.current_emotion];
        if (config->can_blink && config->blink_interval > 0) {
            g_emotion.blink_timer += elapsed_ms;
            if (g_emotion.blink_timer >= config->blink_interval) {
                g_emotion.is_blinking = true;
                g_emotion.blink_frame = 0;
                g_emotion.blink_timer = 0;
            }
        }
    }
    
    // 检查表情是否需要自动切换（如果有持续时间）
    const emotion_config_t* config = &emotion_configs[g_emotion.current_emotion];
    if (config->duration_ms > 0) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (current_time - g_emotion.emotion_start_time >= config->duration_ms) {
            // 自动切换回中性表情
            emotion_set_current(EMOTION_NEUTRAL);
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