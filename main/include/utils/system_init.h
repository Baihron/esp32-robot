#ifndef SYSTEM_INIT_H
#define SYSTEM_INIT_H

#include "esp_err.h"
#include "common_type.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化整个系统
esp_err_t system_init_all(void);

// 反初始化系统
esp_err_t system_deinit_all(void);

// 获取系统配置
const system_config_t* system_get_config(void);

// 检查系统是否已初始化
bool system_is_initialized(void);

// 系统状态字符串
const char* system_get_status_string(void);

#ifdef __cplusplus
}
#endif

#endif // SYSTEM_INIT_H