#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "cam_driver.h"
#include "common_type.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t face_detect_task_init(void);
esp_err_t face_detect_task_start(void);
esp_err_t face_detect_task_stop(void);
#ifdef __cplusplus
}
#endif