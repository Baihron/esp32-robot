#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "common_type.h"
#include "frame_queue.h"
#include "esp_spiffs.h"

#ifdef __cplusplus
extern "C" {
#endif

// 人脸识别结果回调函数类型
typedef void (*face_recognition_callback_t)(bool success, int face_id);

// 初始化人脸识别任务
esp_err_t face_recognition_task_init(void);

// 启动人脸识别任务
esp_err_t face_recognition_task_start(void);

// 停止人脸识别任务
esp_err_t face_recognition_task_stop(void);

// 设置人脸识别结果回调
esp_err_t face_recognition_set_callback(face_recognition_callback_t callback);

// 获取人脸识别任务状态
bool face_recognition_is_running(void);

esp_err_t face_recognition_enroll(frame_data_t* frame);

#ifdef __cplusplus
}
#endif