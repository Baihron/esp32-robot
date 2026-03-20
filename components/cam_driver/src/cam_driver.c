#include "cam_driver.h"
#include "esp_log.h"

static const char *TAG = "CAM_DRIVER";

static camera_config_t camera_config = {
    .pin_pwdn  = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,

    .xclk_freq_hz = CAM_FREQ,

    .pixel_format = PIXFORMAT_RGB565,  // 图像格式
    .frame_size = FRAMESIZE_QVGA,      // 320x240，先别开太大
    .jpeg_quality = 12,
    .fb_count = 2,                    // 双缓冲，连续采集更流畅
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY
};

esp_err_t cam_driver_init(void)
{
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera Init Failed: %s", esp_err_to_name(err));
        return err;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        // 根据实际安装方向调整翻转/镜像
        s->set_vflip(s, 1);
        s->set_hmirror(s, 1);
    }

    ESP_LOGI(TAG, "Camera init OK");
    return ESP_OK;
}

camera_fb_t *cam_capture(void)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera Capture Failed");
    }
    return fb;
}

void cam_return_frame(camera_fb_t *fb)
{
    if (fb) {
        esp_camera_fb_return(fb);
    }
}