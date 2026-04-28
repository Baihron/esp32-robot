#include "eye_tracking.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>
#include "config.h"

static const char *TAG = "EYE_TRACKING";

// 默认配置
static const eye_tracking_config_t DEFAULT_CONFIG = {
    .smooth_factor = 0.5f,         // 平滑因子
    .max_offset_pixels = 60.0f,     // 最大偏移60像素
    .face_lost_timeout = 1000,       // 人脸丢失500ms后重置
    .stable_frames = 1,             // 需要3帧稳定
    .jitter_threshold = 0.2f,      // 抖动阈值5%
    .enable_tracking = true         // 默认启用
};

// 视线追踪状态
static struct {
    eye_tracking_state_t state;
    eye_tracking_config_t config;
    
    // 人脸跟踪辅助变量
    float raw_face_x;               // 原始人脸X位置
    float raw_face_y;               // 原始人脸Y位置
    uint32_t last_face_time;        // 上次检测到人脸的时间
    uint32_t stable_frame_count;    // 稳定帧计数
    float prev_face_x;              // 上一帧人脸X
    float prev_face_y;              // 上一帧人脸Y
    bool tracking_initialized;      // 是否已初始化跟踪
    
} g_eye_tracking = {
    .state = {
        .target_x = 0.0f,
        .target_y = 0.0f,
        .current_x = 0.0f,
        .current_y = 0.0f,
        .eye_offset_x = 0.0f,
        .eye_offset_y = 0.0f,
        .has_face = false,
        .face_valid = false
    },
    .config = DEFAULT_CONFIG,
    .raw_face_x = 0.0f,
    .raw_face_y = 0.0f,
    .last_face_time = 0,
    .stable_frame_count = 0,
    .prev_face_x = 0.0f,
    .prev_face_y = 0.0f,
    .tracking_initialized = false
};

// 初始化视线追踪
void eye_tracking_init(eye_tracking_config_t* config) {
    if (config != NULL) {
        g_eye_tracking.config = *config;
    } else {
        g_eye_tracking.config = DEFAULT_CONFIG;
    }
    
    // 重置状态
    memset(&g_eye_tracking.state, 0, sizeof(eye_tracking_state_t));
    g_eye_tracking.tracking_initialized = false;
    g_eye_tracking.stable_frame_count = 0;
    
    ESP_LOGI(TAG, "Eye tracking initialized (smooth=%f, max_offset=%.0fpx)", 
             g_eye_tracking.config.smooth_factor, 
             g_eye_tracking.config.max_offset_pixels);
}

// 检查帧间抖动
static bool is_jitter_too_large(float current_x, float current_y) {
    if (!g_eye_tracking.tracking_initialized) {
        return false;
    }
    
    float dx = fabs(current_x - g_eye_tracking.prev_face_x);
    float dy = fabs(current_y - g_eye_tracking.prev_face_y);
    
    // 如果任意方向抖动超过阈值，认为是大幅跳动
    return (dx > g_eye_tracking.config.jitter_threshold * 2.0f || 
            dy > g_eye_tracking.config.jitter_threshold * 2.0f);
}

// 更新人脸位置
void eye_tracking_update_face_position(float face_center_x, float face_center_y, 
                                      float face_width, float face_height,
                                      uint32_t timestamp_ms) {
    if (!g_eye_tracking.config.enable_tracking) {
        return;
    }
    
    // 保存原始人脸位置
    g_eye_tracking.raw_face_x = face_center_x;
    g_eye_tracking.raw_face_y = face_center_y;
    g_eye_tracking.last_face_time = timestamp_ms;
    
    ESP_LOGI(TAG, "Raw face position: center=(%.1f, %.1f), size=(%.1f, %.1f)", face_center_x, face_center_y, face_width, face_height);

    // 计算相对于屏幕中心的偏移（归一化到 -1 ~ 1）
    // 假设屏幕尺寸为240x240，中心为(120,120)
    float screen_center_x = 120.0f;
    float screen_center_y = 120.0f;
    
    // 将人脸位置转换为视线偏移
    float new_target_x = (face_center_x - screen_center_x) / screen_center_x;
    float new_target_y = (face_center_y - screen_center_y) / screen_center_y;

#ifdef CONFIG_EYE_TRACKING_INVERT_X
    new_target_x = -new_target_x;
#endif

    // 限制范围
    if (new_target_x > 1.0f) new_target_x = 1.0f;
    if (new_target_x < -1.0f) new_target_x = -1.0f;
    if (new_target_y > 1.0f) new_target_y = 1.0f;
    if (new_target_y < -1.0f) new_target_y = -1.0f;
    
    // 检查是否是大范围跳动（多人脸切换等情况）
    if (is_jitter_too_large(new_target_x, new_target_y)) {
        // 大幅跳动，重置稳定计数，重新开始跟踪
        g_eye_tracking.stable_frame_count = 0;
        ESP_LOGD(TAG, "Large jitter detected, resetting tracking");
    }
    
    // 更新稳定帧计数
    if (g_eye_tracking.tracking_initialized) {
        float dx = fabs(new_target_x - g_eye_tracking.prev_face_x);
        float dy = fabs(new_target_y - g_eye_tracking.prev_face_y);
        
        if (dx < g_eye_tracking.config.jitter_threshold && 
            dy < g_eye_tracking.config.jitter_threshold) {
            g_eye_tracking.stable_frame_count++;
        } else {
            // 有小幅抖动，减少稳定计数但不完全重置
            if (g_eye_tracking.stable_frame_count > 0) {
                g_eye_tracking.stable_frame_count--;
            }
        }
    }
    
    // 更新前一帧位置
    g_eye_tracking.prev_face_x = new_target_x;
    g_eye_tracking.prev_face_y = new_target_y;
    
    // 设置目标位置（只有达到稳定帧数才更新）
    if (g_eye_tracking.stable_frame_count >= g_eye_tracking.config.stable_frames) {
        g_eye_tracking.state.target_x = new_target_x;
        g_eye_tracking.state.target_y = new_target_y;
        g_eye_tracking.state.face_valid = true;
    }
    
    g_eye_tracking.state.has_face = true;
    g_eye_tracking.tracking_initialized = true;
    
    // 平滑处理（低通滤波）
    g_eye_tracking.state.current_x += (g_eye_tracking.state.target_x - g_eye_tracking.state.current_x) * 
                                      g_eye_tracking.config.smooth_factor;
    g_eye_tracking.state.current_y += (g_eye_tracking.state.target_y - g_eye_tracking.state.current_y) * 
                                      g_eye_tracking.config.smooth_factor;
    
    // 计算最终眼睛偏移（像素）
    g_eye_tracking.state.eye_offset_x = g_eye_tracking.state.current_x * 
                                        g_eye_tracking.config.max_offset_pixels;
    g_eye_tracking.state.eye_offset_y = g_eye_tracking.state.current_y * 
                                        g_eye_tracking.config.max_offset_pixels;

    ESP_LOGI(TAG, "Eye offset: (%.1f, %.1f) pixels", 
             g_eye_tracking.state.eye_offset_x, 
             g_eye_tracking.state.eye_offset_y);
}

// 重置视线
void eye_tracking_reset(void) {
    g_eye_tracking.state.target_x = 0.0f;
    g_eye_tracking.state.target_y = 0.0f;
    g_eye_tracking.state.current_x = 0.0f;
    g_eye_tracking.state.current_y = 0.0f;
    g_eye_tracking.state.eye_offset_x = 0.0f;
    g_eye_tracking.state.eye_offset_y = 0.0f;
    g_eye_tracking.state.has_face = false;
    g_eye_tracking.state.face_valid = false;
    g_eye_tracking.tracking_initialized = false;
    g_eye_tracking.stable_frame_count = 0;
    
    ESP_LOGI(TAG, "Eye tracking reset to center");
}

// 获取当前眼睛偏移
void eye_tracking_get_offset(float* offset_x, float* offset_y) {
    if (offset_x) *offset_x = g_eye_tracking.state.eye_offset_x;
    if (offset_y) *offset_y = g_eye_tracking.state.eye_offset_y;
}

// 设置视线追踪启用/禁用
void eye_tracking_set_enabled(bool enabled) {
    g_eye_tracking.config.enable_tracking = enabled;
    if (!enabled) {
        eye_tracking_reset();
    }
    ESP_LOGI(TAG, "Eye tracking %s", enabled ? "enabled" : "disabled");
}

// 检查是否有人脸跟踪
bool eye_tracking_has_face(void) {
    return g_eye_tracking.state.has_face && g_eye_tracking.state.face_valid;
}

// 获取追踪状态
eye_tracking_state_t* eye_tracking_get_state(void) {
    return &g_eye_tracking.state;
}