#ifndef EYE_TRACKING_H
#define EYE_TRACKING_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

// 视线追踪状态
typedef struct {
    float target_x;          // 目标视线X偏移（-1.0 ~ 1.0）
    float target_y;          // 目标视线Y偏移（-1.0 ~ 1.0）
    float current_x;         // 当前视线X偏移（平滑后）
    float current_y;         // 当前视线Y偏移（平滑后）
    float eye_offset_x;      // 最终眼睛偏移X（像素）
    float eye_offset_y;      // 最终眼睛偏移Y（像素）
    bool has_face;           // 是否检测到人脸
    bool face_valid;         // 人脸是否稳定有效
} eye_tracking_state_t;

// 视线追踪配置
typedef struct {
    float smooth_factor;         // 平滑因子（0.01-0.5），越小越平滑
    float max_offset_pixels;     // 最大偏移像素（如20像素）
    uint32_t face_lost_timeout;  // 人脸丢失超时（毫秒）
    uint32_t stable_frames;      // 需要稳定帧数
    float jitter_threshold;      // 抖动阈值（过滤微小跳动）
    bool enable_tracking;        // 是否启用视线追踪
} eye_tracking_config_t;

// 初始化视线追踪
void eye_tracking_init(eye_tracking_config_t* config);

// 更新人脸位置（从检测结果调用）
void eye_tracking_update_face_position(float face_center_x, float face_center_y, 
                                      float face_width, float face_height,
                                      uint32_t timestamp_ms);

// 重置视线（当人脸丢失时调用）
void eye_tracking_reset(void);

// 获取当前眼睛偏移
void eye_tracking_get_offset(float* offset_x, float* offset_y);

// 设置视线追踪启用/禁用
void eye_tracking_set_enabled(bool enabled);

// 检查是否有人脸跟踪
bool eye_tracking_has_face(void);

// 获取追踪状态（用于调试）
eye_tracking_state_t* eye_tracking_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* EYE_TRACKING_H */