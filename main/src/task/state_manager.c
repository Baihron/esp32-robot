#include "state_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "STATE_MANAGER";

// 状态机结构
typedef struct {
    system_state_t current_state;
    system_state_t previous_state;
    bool face_enrolled;           // 是否已录入人脸
    bool powered_on;
    state_change_callback_t callback;
} state_manager_t;

static state_manager_t g_state = {
    .current_state = STATE_SLEEP,
    .previous_state = STATE_SLEEP,
    .face_enrolled = false,
    .powered_on = false,
    .callback = NULL
};

// 状态名称
const char* STATE_NAMES[] = {
    "SLEEP",
    "LOCKED",
    "UNLOCKED",
    "FACE_ENROLLING",
    "SHUTTING_DOWN"
};

// 状态转换表
typedef struct {
    system_state_t from_state;
    system_event_t event;
    system_state_t to_state;
} state_transition_t;

// 状态转换规则
static const state_transition_t TRANSITIONS[] = {
    // 从休眠状态
    {STATE_SLEEP, EVENT_POWER_ON, STATE_LOCKED},       // 长按开机 -> 锁定
    {STATE_SLEEP, EVENT_POWER_ON, STATE_UNLOCKED},     // 长按开机（无人脸） -> 解锁
    
    // 从锁定状态
    {STATE_LOCKED, EVENT_UNLOCK_SUCCESS, STATE_UNLOCKED},
    {STATE_LOCKED, EVENT_POWER_OFF, STATE_SHUTTING_DOWN},
    
    // 从解锁状态
    {STATE_UNLOCKED, EVENT_POWER_OFF, STATE_SHUTTING_DOWN},
    {STATE_UNLOCKED, EVENT_START_ENROLL, STATE_FACE_ENROLLING},
    
    // 从解锁到开始人脸录入
    {STATE_UNLOCKED, EVENT_START_ENROLL, STATE_FACE_ENROLLING},
    {STATE_UNLOCKED, EVENT_POWER_OFF, STATE_SHUTTING_DOWN},

    // 从人脸录入状态
    {STATE_FACE_ENROLLING, EVENT_ENROLL_COMPLETE, STATE_UNLOCKED},
    {STATE_FACE_ENROLLING, EVENT_ENROLL_CANCEL, STATE_UNLOCKED},
    
    // 从关机状态
    {STATE_SHUTTING_DOWN, EVENT_POWER_ON, STATE_SLEEP}
};

// 初始化状态管理器
void state_manager_init(void)
{
    // 初始状态为休眠
    g_state.current_state = STATE_SLEEP;
    g_state.previous_state = STATE_SLEEP;
    
    // 初始化时默认没有录入人脸
    // 实际应该从存储中读取状态
    g_state.face_enrolled = false;
    g_state.powered_on = false;
    g_state.callback = NULL;
    
    ESP_LOGI(TAG, "State manager initialized. Current state: %s", state_manager_get_state_name());
}

// 处理系统事件
void state_manager_handle_event(system_event_t event)
{
    system_state_t old_state = g_state.current_state;
    system_state_t new_state = old_state;
    bool transition_found = false;

    ESP_LOGI(TAG, "Handling event %d in state %s", event, state_manager_get_state_name());

    // 特殊处理：开机事件在休眠状态时根据人脸状态决定进入锁定还是解锁
    if (old_state == STATE_SLEEP && event == EVENT_POWER_ON) {
        if (g_state.face_enrolled) {
            new_state = STATE_LOCKED;
            g_state.powered_on = true;
            ESP_LOGI(TAG, "Face enrolled, entering LOCKED state");
        } else {
            new_state = STATE_UNLOCKED;
            g_state.powered_on = true;
            ESP_LOGI(TAG, "No face enrolled, entering UNLOCKED state");
        }
        transition_found = true;
    }
    // 特殊处理：关机事件在锁定或解锁状态时进入关机状态
    else if ((old_state == STATE_LOCKED || old_state == STATE_UNLOCKED) && event == EVENT_POWER_OFF) {
        new_state = STATE_SHUTTING_DOWN;
        transition_found = true;
    }
    // 其他情况查找转换表
    else {
        for (size_t i = 0; i < sizeof(TRANSITIONS) / sizeof(TRANSITIONS[0]); i++) {
            if (TRANSITIONS[i].from_state == old_state && TRANSITIONS[i].event == event) {
                new_state = TRANSITIONS[i].to_state;
                transition_found = true;

                // 处理特殊状态转换逻辑
                if (new_state == STATE_UNLOCKED && event == EVENT_ENROLL_COMPLETE) {
                    g_state.face_enrolled = true;
                    ESP_LOGI(TAG, "Face enrolled successfully");
                } else if(new_state == STATE_UNLOCKED && event == EVENT_ENROLL_CANCEL) {
                    g_state.face_enrolled = false;
                    ESP_LOGI(TAG, "Face enrollment canceled");
                } else if (new_state == STATE_SLEEP) {
                    g_state.powered_on = false;
                    ESP_LOGI(TAG, "System powered off, entering sleep");
                }

                break;
            }
        }
    }
    
    if (!transition_found) {
        ESP_LOGW(TAG, "No valid transition for event %d in state %s", event, state_manager_get_state_name());
        return;
    }

    // 更新状态
    g_state.previous_state = old_state;
    g_state.current_state = new_state;

    ESP_LOGI(TAG, "State changed: %s -> %s", STATE_NAMES[old_state], STATE_NAMES[new_state]);

    // 调用回调函数
    if (g_state.callback != NULL) {
        g_state.callback(old_state, new_state);
    }
}

// 获取当前状态
system_state_t state_manager_get_state(void)
{
    return g_state.current_state;
}

// 获取状态名称
const char* state_manager_get_state_name(void)
{
    return STATE_NAMES[g_state.current_state];
}

// 检查是否需要人脸检测
bool state_manager_need_face_detection(void)
{
    // 在锁定状态和人脸录入状态需要人脸检测
    return (g_state.current_state == STATE_LOCKED || g_state.current_state == STATE_FACE_ENROLLING);
}

// 检查系统是否上电
bool state_manager_is_powered_on(void)
{
    return g_state.powered_on;
}

// 设置人脸录入状态
void state_manager_set_face_enrolled(bool enrolled)
{
    g_state.face_enrolled = enrolled;
    ESP_LOGI(TAG, "Face enrolled status set to: %s", enrolled ? "ENROLLED" : "NOT_ENROLLED");
}

// 获取人脸录入状态
bool state_manager_is_face_enrolled(void)
{
    return g_state.face_enrolled;
}

// 注册状态改变回调
void state_manager_register_callback(state_change_callback_t callback)
{
    g_state.callback = callback;
    ESP_LOGI(TAG, "State change callback registered");
}