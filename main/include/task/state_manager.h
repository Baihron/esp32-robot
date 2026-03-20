#ifndef STATE_MANAGER_H
#define STATE_MANAGER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 系统状态定义
typedef enum {
    STATE_SLEEP = 0,        // 休眠状态，所有任务关闭
    STATE_LOCKED,           // 锁定状态，需要人脸识别解锁
    STATE_UNLOCKED,         // 解锁状态，正常使用
    STATE_FACE_ENROLLING,   // 人脸录入中
    STATE_SHUTTING_DOWN     // 关机中
} system_state_t;

// 系统事件定义
typedef enum {
    EVENT_POWER_ON = 0,     // 开机（按钮长按）
    EVENT_POWER_OFF,        // 关机（按钮长按）
    EVENT_UNLOCK_SUCCESS,   // 解锁成功
    EVENT_START_ENROLL,     // 开始人脸录入（按钮双击）
    EVENT_ENROLL_COMPLETE,  // 人脸录入完成
    EVENT_ENROLL_CANCEL     // 人脸录入取消
} system_event_t;

// 初始化状态管理器
void state_manager_init(void);

// 处理系统事件
void state_manager_handle_event(system_event_t event);

// 获取当前状态
system_state_t state_manager_get_state(void);

// 获取状态名称
const char* state_manager_get_state_name(void);

// 检查是否需要人脸检测
bool state_manager_need_face_detection(void);

// 检查系统是否上电
bool state_manager_is_powered_on(void);

// 设置人脸录入状态
void state_manager_set_face_enrolled(bool enrolled);

// 获取人脸录入状态
bool state_manager_is_face_enrolled(void);

// 注册状态改变回调
typedef void (*state_change_callback_t)(system_state_t old_state, system_state_t new_state);
void state_manager_register_callback(state_change_callback_t callback);

#ifdef __cplusplus
}
#endif

#endif // STATE_MANAGER_H