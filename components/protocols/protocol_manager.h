// components/protocols/protocol_manager.h
#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t protocol_manager_init(void);
esp_err_t protocol_manager_start(void);
esp_err_t protocol_manager_stop(void);
bool protocol_manager_is_running(void);

#ifdef __cplusplus
}
#endif