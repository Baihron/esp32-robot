#include "state_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include "esp_partition.h"

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
    {STATE_LOCKED, EVENT_POWER_OFF, STATE_SLEEP},
    
    // 从解锁状态
    {STATE_UNLOCKED, EVENT_POWER_OFF, STATE_SLEEP},
    {STATE_UNLOCKED, EVENT_START_ENROLL, STATE_FACE_ENROLLING},
    
    // 从解锁到开始人脸录入
    {STATE_UNLOCKED, EVENT_START_ENROLL, STATE_FACE_ENROLLING},
    {STATE_UNLOCKED, EVENT_POWER_OFF, STATE_SLEEP},

    // 从人脸录入状态
    {STATE_FACE_ENROLLING, EVENT_ENROLL_COMPLETE, STATE_UNLOCKED},
    {STATE_FACE_ENROLLING, EVENT_ENROLL_CANCEL, STATE_UNLOCKED},
    
    // 从关机状态
    {STATE_SHUTTING_DOWN, EVENT_POWER_ON, STATE_SLEEP}
};

static bool check_face_data_in_flash(void)
{
    // return false;
    // 查找人脸图片分区
    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        0xff,  // 子类型
        "face_img"  // 分区名称，与flash_driver.c中一致
    );
    
    if (!partition) {
        ESP_LOGW(TAG, "Face image partition not found");
        return false;
    }
    
    ESP_LOGI(TAG, "Checking face data in partition: %s, size: %lu bytes", partition->label, partition->size);
    
    // 根据flash_driver.c中的定义
    #define SLOT_SIZE           (256 * 1024)      // 每个槽位256KB
    #define SLOT_HEADER_SIZE    2                 // 头部大小（图片大小）
    
    int max_slots = partition->size / SLOT_SIZE;
    
    // 检查每个槽位
    for (int slot = 0; slot < max_slots; slot++) {
        size_t slot_offset = slot * SLOT_SIZE;
        
        // 读取头部（图片大小）
        uint32_t img_size = 0;
        esp_err_t ret = esp_partition_read(partition, slot_offset, &img_size, sizeof(img_size));
        
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read slot %d header: %d", slot, ret);
            continue;
        }
        
        // 验证图片大小是否合理
        // 根据face_detect_task.cpp中的逻辑，图片可能是：
        // 1. JPEG格式（直接保存）
        // 2. RGB565格式（转换为BMP保存）
        // 3. RGB888格式（直接保存）
        
        // 合理的图片大小范围（根据实际应用调整）
        // 最小：假设至少10KB
        // 最大：槽位大小减去头部
        if (img_size >= 10 * 1024 && img_size <= (SLOT_SIZE - SLOT_HEADER_SIZE)) {
            ESP_LOGI(TAG, "Found valid face image in slot %d: size=%lu bytes", slot, img_size);
            
            // 可选：进一步验证图片数据
            // 读取部分数据验证格式
            uint8_t header_buffer[64];
            ret = esp_partition_read(partition, slot_offset + SLOT_HEADER_SIZE, 
                                    header_buffer, sizeof(header_buffer));
            
            if (ret == ESP_OK) {
                // 检查常见的图片格式标记
                bool valid_format = false;
                
                // 检查JPEG格式（FF D8开头）
                if (header_buffer[0] == 0xFF && header_buffer[1] == 0xD8) {
                    ESP_LOGI(TAG, "  Format: JPEG");
                    valid_format = true;
                }
                // 检查BMP格式（'BM'开头）
                else if (header_buffer[0] == 'B' && header_buffer[1] == 'M') {
                    ESP_LOGI(TAG, "  Format: BMP");
                    valid_format = true;
                }
                // 检查PNG格式
                else if (header_buffer[0] == 0x89 && header_buffer[1] == 'P' && 
                         header_buffer[2] == 'N' && header_buffer[3] == 'G') {
                    ESP_LOGI(TAG, "  Format: PNG");
                    valid_format = true;
                }
                // 检查RGB565/RGB888格式（没有特定标记，但数据应该不是全0或全0xFF）
                else {
                    // 检查数据是否有效（不是擦除状态）
                    bool all_zeros = true;
                    bool all_ff = true;
                    
                    for (int i = 0; i < sizeof(header_buffer); i++) {
                        if (header_buffer[i] != 0x00) all_zeros = false;
                        if (header_buffer[i] != 0xFF) all_ff = false;
                    }
                    
                    if (!all_zeros && !all_ff) {
                        ESP_LOGI(TAG, "  Format: Raw image data (RGB565/RGB888)");
                        valid_format = true;
                    }
                }
                
                if (valid_format) {
                    return true;  // 找到有效的人脸图片
                }
            }
        } else if (img_size != 0xFFFFFFFF && img_size != 0) {
            // 如果大小不合理但不是擦除状态，记录警告
            ESP_LOGW(TAG, "Invalid image size in slot %d: %lu bytes", slot, img_size);
        }
    }
    
    ESP_LOGI(TAG, "No valid face image data found in any slot");
    return false;
}

// 初始化状态管理器
void state_manager_init(void)
{
    // 初始状态为休眠
    g_state.current_state = STATE_SLEEP;
    g_state.previous_state = STATE_SLEEP;
    
    // 初始化时检查是否有已录入的人脸
    g_state.face_enrolled = check_face_data_in_flash();
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
        new_state = STATE_SLEEP;
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