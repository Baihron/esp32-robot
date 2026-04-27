#include "face_detect_task.h"

#include "esp_log.h"

#include "frame_queue.h"
#include "camera_task.h"
#include "display_task.h"
#include "fs_driver.h"
#include "flash_driver.h"
#include "eye_tracking.h"

// esp-dl 相关头文件
#include "human_face_detect.hpp"
#include "human_face_recognition.hpp"
#include "dl_image_define.hpp"
#include "dl_recognition_define.hpp"

#define CAPTURE_FRAME_COUNT 5      // 捕获帧数
#define VALID_FACE_THRESHOLD 3     // 有效帧阈值
#define JPEG_QUALITY 90            // JPEG质量（1-100）
#define FACE_DETECTION_THRESHOLD 0.6f  // 人脸检测阈值

using namespace dl::image;

static const char *TAG_FACE = "FACE_DETECT";

extern task_status_t g_tasks;

// 人脸检测
static HumanFaceDetect *s_face_detect = nullptr;

// 任务句柄
static TaskHandle_t s_face_task_handle = nullptr;

// 新增结构体保存帧信息
typedef struct {
    frame_data_t* frame;
    float max_face_score;
    int face_count;
    bool has_face;
} capture_frame_info_t;

// 新增：释放帧信息数组
static void free_capture_frames(capture_frame_info_t* frames, int count)
{
    for (int i = 0; i < count; i++) {
        if (frames[i].frame) {
            frame_queue_release_data(frames[i].frame);
            frames[i].frame = nullptr;
        }
    }
}

/**
 * @brief camera_fb_t -> dl::image::img_t
 */
static inline img_t fb_to_img(camera_fb_t *fb)
{
    img_t img;
    img.data    = fb->buf;
    img.width   = fb->width;
    img.height  = fb->height;

    if (fb->format == PIXFORMAT_RGB565) {
        img.pix_type = DL_IMAGE_PIX_TYPE_RGB565;
        // ESP_LOGI(TAG_FACE, "Image format: RGB565");
    } else if (fb->format == PIXFORMAT_RGB888) {
        img.pix_type = DL_IMAGE_PIX_TYPE_RGB888;
        // ESP_LOGI(TAG_FACE, "Image format: RGB888");
    } else {
        img.pix_type = DL_IMAGE_PIX_TYPE_RGB565;
        // ESP_LOGW(TAG_FACE, "Unknown format %d, using RGB565", fb->format);
    }

    return img;
}

/**
 * @brief 初始化人脸检测和识别模块
 */
static esp_err_t face_ai_init(void)
{
    if (!s_face_detect) {
        ESP_LOGI(TAG_FACE, "Creating face detector...");
        s_face_detect = new HumanFaceDetect();
        if (!s_face_detect) {
            return ESP_FAIL;
        }
        ESP_LOGI(TAG_FACE, "Face detector created successfully");
    }

    return ESP_OK;
}

static void process_face_detect(void)
{
    // 1. 创建帧信息数组
    capture_frame_info_t captured_frames[CAPTURE_FRAME_COUNT] = {0};
    int valid_face_frames = 0;
    int best_frame_index = -1;
    float best_face_score = 0.0f;
    
    // 2. 捕获5帧并进行人脸检测
    for (int i = 0; i < CAPTURE_FRAME_COUNT; i++) {
        // ESP_LOGI(TAG_FACE, "Capturing frame %d/%d...", i + 1, CAPTURE_FRAME_COUNT);

        // 从队列获取帧
        captured_frames[i].frame = frame_queue_receive_data(pdMS_TO_TICKS(100));
        
        if (!captured_frames[i].frame) {
            ESP_LOGW(TAG_FACE, "Failed to get frame %d", i + 1);
            continue;
        }

        // ESP_LOGI(TAG_FACE, "Frame %d: %dx%d, format: %d, size: %d bytes", i + 1, captured_frames[i].frame->width, captured_frames[i].frame->height, captured_frames[i].frame->format, captured_frames[i].frame->size);

        // 3. 创建临时camera_fb_t并转换为img_t
        camera_fb_t temp_fb;
        temp_fb.buf = captured_frames[i].frame->data;
        temp_fb.len = captured_frames[i].frame->size;
        temp_fb.width = captured_frames[i].frame->width;
        temp_fb.height = captured_frames[i].frame->height;

        // 转换格式枚举
        switch (captured_frames[i].frame->format) {
            case 1: temp_fb.format = PIXFORMAT_RGB565; break;
            case 2: temp_fb.format = PIXFORMAT_RGB888; break;
            case 3: temp_fb.format = PIXFORMAT_JPEG; break;
            default: temp_fb.format = PIXFORMAT_RGB565; break;
        }
        
        struct timeval tv;
        gettimeofday(&tv, NULL);
        temp_fb.timestamp = tv;
        
        // 转换为img_t格式
        img_t img = fb_to_img(&temp_fb);

        // 4. 人脸检测
        // ESP_LOGI(TAG_FACE, "Running face detection on frame %d...", i + 1);
        auto det_res = s_face_detect->run(img);
        
        // 统计检测结果
        int face_count = 0;
        float max_score = 0.0f;
        
        for (auto it = det_res.begin(); it != det_res.end(); ++it) {
            face_count++;
            if (it->score > max_score) {
                max_score = it->score;
            }

            if (it->score >= FACE_DETECTION_THRESHOLD) {
            // 计算人脸中心
                float face_center_x = (it->box[0] + it->box[2]) / 2.0f;
                float face_center_y = (it->box[1] + it->box[3]) / 2.0f;
                float face_width = it->box[2] - it->box[0];
                float face_height = it->box[3] - it->box[1];
                
                // 更新视线追踪（假设屏幕尺寸240x240）
                uint32_t timestamp = esp_timer_get_time() / 1000; // 转换为毫秒
                eye_tracking_update_face_position(face_center_x, face_center_y, 
                                                face_width, face_height,
                                                timestamp);
            }

            ESP_LOGI(TAG_FACE, "Face %d: Score=%.3f, Box=[%d,%d,%d,%d]", face_count, it->score, it->box[0], it->box[1], it->box[2], it->box[3]);
        }

        // 保存检测结果
        captured_frames[i].face_count = face_count;
        captured_frames[i].max_face_score = max_score;
        captured_frames[i].has_face = (face_count > 0) && (max_score >= FACE_DETECTION_THRESHOLD);
        
        if (captured_frames[i].has_face) {
            valid_face_frames++;
            ESP_LOGI(TAG_FACE, "Frame %d: ✓ Valid face (Score=%.3f)", i + 1, max_score);
            
            // 更新最佳帧
            if (max_score > best_face_score) {
                best_face_score = max_score;
                best_frame_index = i;
            }
        } else {
            // ESP_LOGI(TAG_FACE, "Frame %d: ✗ No valid face", i + 1);
        }

        // 短暂延迟，确保帧之间有间隔
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // 5. 有效性判断：至少3帧检测到有效人脸
    if (valid_face_frames < VALID_FACE_THRESHOLD) {
        // ESP_LOGI(TAG_FACE, "Face validation FAILED: %d/%d frames have valid faces", valid_face_frames, CAPTURE_FRAME_COUNT);
        free_capture_frames(captured_frames, CAPTURE_FRAME_COUNT);
    } else {
        ESP_LOGI(TAG_FACE, "Face validation PASSED: %d/%d frames have valid faces", valid_face_frames, CAPTURE_FRAME_COUNT);
        ESP_LOGI(TAG_FACE, "Best frame: #%d (Score=%.3f)", best_frame_index + 1, best_face_score);
    }

    // 保存数据
    static uint32_t start_time  = 0;
    static uint32_t current_time  = 0;
    static uint8_t enter_enroll_flag = 0;

    if(state_manager_get_state() == STATE_FACE_ENROLLING) {
        if(enter_enroll_flag == 0) {
            start_time = esp_timer_get_time();
            enter_enroll_flag = 1;
        }

        if (best_frame_index >= 0 && captured_frames[best_frame_index].frame) {
            frame_data_t* best_frame = captured_frames[best_frame_index].frame;
            g_tasks.camera_running = false;

            ESP_LOGI(TAG_FACE, "Best frame ready for flash saving: %dx%d, format: %d", best_frame->width, best_frame->height, best_frame->format);

            esp_err_t ret = face_flash_storage_save(best_frame->data, best_frame->size, 0);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG_FACE, "✓ Face saved to flash successfully (%zu bytes)", best_frame->size);

                face_recognition_enroll(best_frame);

            } else {
                ESP_LOGE(TAG_FACE, "Failed to save face to flash: %d", ret);
            }
            g_tasks.camera_running = true;

            ESP_LOGI(TAG_FACE, "Best frame ready for flash saving: %dx%d, format: %d", best_frame->width, best_frame->height, best_frame->format);

            // 切换模式
            enter_enroll_flag = 0;
            state_manager_handle_event(EVENT_ENROLL_COMPLETE);
        }
        current_time = esp_timer_get_time();

        if(current_time - start_time > 5000000) {
            enter_enroll_flag = 0;
            state_manager_handle_event(EVENT_ENROLL_CANCEL);
        }
    }

    // 7. 释放所有帧资源
    free_capture_frames(captured_frames, CAPTURE_FRAME_COUNT);
}

static void face_app_task(void *arg)
{
    if (face_ai_init() != ESP_OK) {
        ESP_LOGE(TAG_FACE, "Face AI initialization failed");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG_FACE, "Face recognition task ready. Waiting for button press...");
    
    while (1) {
        if (g_tasks.face_detection_running) {

            // if (!check_sd_card_status()) {
            //     ESP_LOGE(TAG_FACE, "SD卡未就绪，无法保存人脸图像");
            //     continue;
            // }

            // 检查是否长时间没有检测到人脸
            static uint32_t last_detection_time = 0;
            uint32_t current_time = esp_timer_get_time() / 1000;
            
            if (last_detection_time > 0 && 
                current_time - last_detection_time > 1000) { // 1秒没检测到
                // 重置视线追踪，让眼睛回到中心
                eye_tracking_reset();
            }

            if(!g_tasks.face_recognition_running) {
                process_face_detect();
                last_detection_time = current_time;
            } else {
#ifdef CONFIG_DEBUG_PRINT
                // ESP_LOGI(TAG_FACE, "人脸识别任务正在运行，请勿重复操作");
#endif
            }
        }

        // 短延迟
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* 对外启动接口 */
esp_err_t face_detect_task_init(void)
{
    if (s_face_task_handle) {
        ESP_LOGW(TAG_FACE, "Face recognition task already started");
        return ESP_OK;
    }

    BaseType_t ret = xTaskCreatePinnedToCore(
        face_app_task,
        "face_app_task",
        1024 * 12,  // 增加栈大小
        NULL,
        tskIDLE_PRIORITY + 3,
        &s_face_task_handle,
        0
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG_FACE, "Failed to create face recognition task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG_FACE, "Face recognition task created successfully");
    return ESP_OK;
}
esp_err_t face_detect_task_start(void)
{
    if(g_tasks.face_detection_running) {
        ESP_LOGW(TAG_FACE, "Face detection task already running");
        return ESP_OK;
    }

    g_tasks.face_detection_running = true;

    return ESP_OK;
}

esp_err_t face_detect_task_stop(void)
{
    if(!g_tasks.face_detection_running) {
        ESP_LOGW(TAG_FACE, "Face detection task not running");
        return ESP_OK;
    }

    g_tasks.face_detection_running = false;

    return ESP_OK;
}