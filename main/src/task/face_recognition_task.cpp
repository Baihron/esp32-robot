#include "face_recognition_task.h"
#include "esp_log.h"
#include "frame_queue.h"
#include "state_manager.h"
#include "task_controller.h"

// esp-dl 相关头文件
#include "human_face_detect.hpp"
#include "human_face_recognition.hpp"
#include "dl_image_define.hpp"
#include "dl_recognition_define.hpp"

using namespace dl::image;

static const char *TAG = "FACE_RECOGNITION";

// 任务句柄
static TaskHandle_t s_recognition_task_handle = nullptr;

// 人脸检测和识别对象
static HumanFaceDetect    *s_face_detect    = nullptr;
static HumanFaceRecognizer *s_face_recognizer = nullptr;

// 回调函数
static face_recognition_callback_t s_callback = nullptr;

// 任务状态（使用外部 g_tasks）
extern task_status_t g_tasks;

// 相似度阈值
static const float SIMILARITY_THRESHOLD = 0.7f;

// 最大识别尝试次数
static const int MAX_RECOGNITION_ATTEMPTS = 50;
static int s_recognition_attempts = 0;

/**
 * @brief camera_fb_t -> dl::image::img_t
 */
static inline img_t fb_to_img(camera_fb_t *fb)
{
    img_t img;
    img.data   = fb->buf;
    img.width  = fb->width;
    img.height = fb->height;

    if (fb->format == PIXFORMAT_RGB565) {
        img.pix_type = DL_IMAGE_PIX_TYPE_RGB565;
    } else if (fb->format == PIXFORMAT_RGB888) {
        img.pix_type = DL_IMAGE_PIX_TYPE_RGB888;
    } else {
        img.pix_type = DL_IMAGE_PIX_TYPE_RGB565;
    }

    return img;
}

/**
 * @brief 初始化人脸检测和识别模块
 *        数据库路径根据您的文件系统配置修改（SPIFFS / FAT / SDCard）
 */
static esp_err_t face_recognition_ai_init(void)
{
    if (!s_face_detect) {
        ESP_LOGI(TAG, "Creating face detector...");
        s_face_detect = new HumanFaceDetect();
        if (!s_face_detect) {
            ESP_LOGE(TAG, "Failed to create face detector");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Face detector created successfully");
    }

    // 第一步：挂载 SPIFFS
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "face_db",
        .max_files = 2,
        .format_if_mount_failed = true
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        return ESP_FAIL;
    }

    if (!s_face_recognizer) {
        ESP_LOGI(TAG, "Creating face recognizer...");
        // 传入数据库路径，根据实际文件系统修改路径
        s_face_recognizer = new HumanFaceRecognizer("/spiffs/face_db");
        if (!s_face_recognizer) {
            ESP_LOGE(TAG, "Failed to create face recognizer");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Face recognizer created successfully");
    }

    return ESP_OK;
}

/**
 * @brief 执行单次人脸识别
 */
static bool perform_face_recognition(void)
{
    ESP_LOGI(TAG, "Performing face recognition...");

    s_recognition_attempts++;
    // 1. 从队列获取最新帧
    frame_data_t *frame_data = frame_queue_receive_data(pdMS_TO_TICKS(100));
    if (!frame_data) {
        ESP_LOGW(TAG, "No frame available for recognition");
        return false;
    }

    // ESP_LOGI(TAG, "Frame for recognition: %dx%d, format: %d", frame_data->width, frame_data->height, frame_data->format);

    // 2. 创建临时 camera_fb_t
    camera_fb_t temp_fb;
    temp_fb.buf    = frame_data->data;
    temp_fb.len    = frame_data->size;
    temp_fb.width  = frame_data->width;
    temp_fb.height = frame_data->height;

    switch (frame_data->format) {
        case 1:  temp_fb.format = PIXFORMAT_RGB565; break;
        case 2:  temp_fb.format = PIXFORMAT_RGB888; break;
        default: temp_fb.format = PIXFORMAT_RGB565; break;
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);
    temp_fb.timestamp = tv;

    // 3. 转换为 img_t 格式
    img_t img = fb_to_img(&temp_fb);

    // 4. 人脸检测
    auto det_res = s_face_detect->run(img);
    if (det_res.empty()) {
        ESP_LOGW(TAG, "No face detected in frame");
        frame_queue_release_data(frame_data);
        return false;
    }

    ESP_LOGI(TAG, "Detected %d face(s) in frame", (int)det_res.size());

    g_tasks.camera_running = false;
    // 5. 人脸识别（与数据库中的特征比对）
    bool recognition_success = false;
    std::vector<dl::recognition::result_t> res = s_face_recognizer->recognize(img, det_res);

    g_tasks.camera_running = true;
    if (!res.empty()) {
        int   matched_id      = res[0].id;
        float best_similarity = res[0].similarity;

        ESP_LOGI(TAG, "Recognition result: ID=%d, Similarity=%.3f", matched_id, best_similarity);

        if (best_similarity > SIMILARITY_THRESHOLD) {
            recognition_success = true;
            ESP_LOGI(TAG, "Face MATCHED! (ID: %d, Similarity: %.3f)", matched_id, best_similarity);
        } else {
            ESP_LOGI(TAG, "Face detected but similarity too low (%.3f <= %.3f)", best_similarity, SIMILARITY_THRESHOLD);
        }
    } else {
        ESP_LOGI(TAG, "No matching face found in database");
    }

    // 6. 释放帧
    frame_queue_release_data(frame_data);

    // 7. 更新识别尝试次数
    if (recognition_success) {
        s_recognition_attempts = 0;
    } else {
        s_recognition_attempts++;
        ESP_LOGI(TAG, "Recognition attempt %d/%d failed", s_recognition_attempts, MAX_RECOGNITION_ATTEMPTS);
    }

    return recognition_success;
}

/**
 * @brief 人脸识别任务主函数
 */
static void face_recognition_task_func(void *arg)
{
    ESP_LOGI(TAG, "Face recognition task started");

    g_tasks.face_recognition_initialized = true;
    s_recognition_attempts = 0;

    while (1) {
        if (g_tasks.face_recognition_running) {
            ESP_LOGI(TAG, "=== Starting recognition cycle ===");

            bool success = perform_face_recognition();

            if (success) {
                ESP_LOGI(TAG, "Face recognition SUCCESS! Unlocking system...");

                if (s_callback) {
                    s_callback(true, 0);
                }

                state_manager_handle_event(EVENT_UNLOCK_SUCCESS);

                // 识别成功后停止任务
                g_tasks.face_recognition_running = false;

            } else {
                ESP_LOGI(TAG, "Face recognition FAILED");

                if (s_callback) {
                    s_callback(false, -1);
                }

                // 达到最大尝试次数，停止识别
                if (s_recognition_attempts >= MAX_RECOGNITION_ATTEMPTS) {
                    ESP_LOGW(TAG, "Max recognition attempts reached (%d), stopping",
                             MAX_RECOGNITION_ATTEMPTS);
                    g_tasks.face_recognition_running = false;
                }
            }

            ESP_LOGI(TAG, "=== Recognition cycle completed ===");
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ---- 对外接口实现 ---- */

esp_err_t face_recognition_task_init(void)
{
    if (s_recognition_task_handle) {
        ESP_LOGW(TAG, "Face recognition task already initialized");
        return ESP_OK;
    }

    face_recognition_ai_init();

    BaseType_t ret = xTaskCreatePinnedToCore(
        face_recognition_task_func,
        "face_recognition_task",
        16384,
        NULL,
        tskIDLE_PRIORITY + 4,
        &s_recognition_task_handle,
        0
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create face recognition task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Face recognition task created successfully");
    return ESP_OK;
}

esp_err_t face_recognition_task_start(void)
{
    if (!g_tasks.face_recognition_initialized) {
        ESP_LOGE(TAG, "Face recognition task not initialized");
        return ESP_FAIL;
    }

    if (g_tasks.face_recognition_running) {
        ESP_LOGW(TAG, "Face recognition task already running");
        return ESP_OK;
    }

    s_recognition_attempts = 0;
    g_tasks.face_recognition_running = true;

    ESP_LOGI(TAG, "Face recognition task started");
    return ESP_OK;
}

esp_err_t face_recognition_task_stop(void)
{
    if (!g_tasks.face_recognition_running) {
        ESP_LOGW(TAG, "Face recognition task not running");
        return ESP_OK;
    }

    g_tasks.face_recognition_running = false;
    ESP_LOGI(TAG, "Face recognition task stopped");
    return ESP_OK;
}

esp_err_t face_recognition_set_callback(face_recognition_callback_t callback)
{
    s_callback = callback;
    ESP_LOGI(TAG, "Face recognition callback set");
    return ESP_OK;
}

bool face_recognition_is_running(void)
{
    return g_tasks.face_recognition_running;
}

esp_err_t face_recognition_enroll(frame_data_t* frame)
{
    if (!s_face_detect || !s_face_recognizer) {
        ESP_LOGE(TAG, "AI modules not initialized");
        return ESP_FAIL;
    }

    camera_fb_t temp_fb;
    temp_fb.buf    = frame->data;
    temp_fb.len    = frame->size;
    temp_fb.width  = frame->width;
    temp_fb.height = frame->height;

    switch (frame->format) {
        case 1:  temp_fb.format = PIXFORMAT_RGB565; break;
        case 2:  temp_fb.format = PIXFORMAT_RGB888; break;
        default: temp_fb.format = PIXFORMAT_RGB565; break;
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);
    temp_fb.timestamp = tv;

    img_t img = fb_to_img(&temp_fb);

    // 先检测人脸，再注册
    auto det_res = s_face_detect->run(img);
    if (det_res.empty()) {
        ESP_LOGE(TAG, "No face detected during enroll");
        return ESP_FAIL;
    }

    s_face_recognizer->enroll(img, det_res);
    ESP_LOGI(TAG, "Face enrolled successfully");
    return ESP_OK;
}