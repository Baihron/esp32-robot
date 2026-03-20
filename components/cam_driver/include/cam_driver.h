#pragma once
#include "esp_err.h"
#include "esp_camera.h"
#include "cam_config.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t cam_driver_init(void);
camera_fb_t *cam_capture(void);
void cam_return_frame(camera_fb_t *fb);

#ifdef __cplusplus
}
#endif