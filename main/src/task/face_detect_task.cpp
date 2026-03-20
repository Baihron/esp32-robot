#include "face_detect_task.h"

#include "esp_log.h"

#include "frame_queue.h"
#include "camera_task.h"
#include "display_task.h"
#include "fs_driver.h"

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

static const char *TAG_FACE = "FACE_APP";

extern task_status_t g_tasks;

// 人脸检测 + 识别对象
static HumanFaceDetect *s_face_detect = nullptr;
static HumanFaceRecognizer *s_face_recognizer = nullptr;

// 任务句柄
static TaskHandle_t s_face_task_handle = nullptr;

// 人脸数据库状态
static int s_face_count = 0;

// 新增结构体保存帧信息
typedef struct {
    frame_data_t* frame;
    float max_face_score;
    int face_count;
    bool has_face;
} capture_frame_info_t;


// 新增：创建文件名（基于时间戳）
static char* generate_filename(const char* prefix, const char* ext)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    // 格式：prefix_年-月-日_时-分-秒.ext
    struct tm* timeinfo = localtime(&tv.tv_sec);
    static char filename[64];
    snprintf(filename, sizeof(filename), 
             "/sdcard/%s_%04d%02d%02d_%02d%02d%02d.%s",
             prefix,
             timeinfo->tm_year + 1900,
             timeinfo->tm_mon + 1,
             timeinfo->tm_mday,
             timeinfo->tm_hour,
             timeinfo->tm_min,
             timeinfo->tm_sec,
             ext);
    
    return filename;
}

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
 * @brief RGB565到RGB888转换
 */
static camera_fb_t* convert_rgb565_to_rgb888(camera_fb_t *rgb565_fb)
{
    if (!rgb565_fb || rgb565_fb->format != PIXFORMAT_RGB565) {
        return rgb565_fb;
    }
    
    // 分配新的缓冲区（3倍大小，因为RGB888每像素3字节）
    size_t rgb888_size = rgb565_fb->width * rgb565_fb->height * 3;
    uint8_t *rgb888_buf = (uint8_t*)heap_caps_malloc(rgb888_size, MALLOC_CAP_SPIRAM);
    
    if (!rgb888_buf) {
        ESP_LOGE(TAG_FACE, "Failed to allocate RGB888 buffer");
        return rgb565_fb;
    }
    
    // 转换每个像素
    uint16_t *rgb565 = (uint16_t*)rgb565_fb->buf;
    uint8_t *rgb888 = rgb888_buf;
    
    for (int i = 0; i < rgb565_fb->width * rgb565_fb->height; i++) {
        uint16_t pixel = rgb565[i];
        
        // 提取RGB565分量
        uint8_t r5 = (pixel >> 11) & 0x1F;
        uint8_t g6 = (pixel >> 5) & 0x3F;
        uint8_t b5 = pixel & 0x1F;
        
        // 扩展为RGB888 (5位扩展到8位: value * 255 / 31)
        *rgb888++ = (r5 * 527 + 23) >> 6;  // r
        *rgb888++ = (g6 * 259 + 33) >> 6;  // g
        *rgb888++ = (b5 * 527 + 23) >> 6;  // b
    }
    
    // 创建新的cam_fb_t结构
    camera_fb_t *rgb888_fb = (camera_fb_t*)malloc(sizeof(camera_fb_t));
    memcpy(rgb888_fb, rgb565_fb, sizeof(camera_fb_t));
    rgb888_fb->buf = rgb888_buf;
    rgb888_fb->len = rgb888_size;
    rgb888_fb->format = PIXFORMAT_RGB888;
    
    return rgb888_fb;
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

/**
 * @brief 处理人脸识别和录入
 */
// static void process_face(void)
// {
//     ESP_LOGI(TAG_FACE, "=== Starting face processing ===");
    
//     // 1. 从队列获取最新帧
//     ESP_LOGI(TAG_FACE, "Getting frame from queue...");
//     frame_data_t *frame_data = frame_queue_receive_data(pdMS_TO_TICKS(0));
    
//     if (!frame_data) {
//         ESP_LOGW(TAG_FACE, "No frame in queue");
//         return;
//     }

//     ESP_LOGI(TAG_FACE, "Frame info: %dx%d, format: %d, size: %d bytes", 
//              frame_data->width, frame_data->height, frame_data->format, frame_data->size);
    
//     // 检查图像数据
//     if (frame_data->data == NULL || frame_data->size == 0) {
//         ESP_LOGE(TAG_FACE, "Invalid frame data");
//         frame_queue_release_data(frame_data);
//         return;
//     }

//     // 2. 创建临时 camera_fb_t 用于转换
//     camera_fb_t temp_fb;
//     temp_fb.buf = frame_data->data;
//     temp_fb.len = frame_data->size;
//     temp_fb.width = frame_data->width;
//     temp_fb.height = frame_data->height;
    
//     // 转换格式枚举
//     switch (frame_data->format) {
//         case 1:  // PIXFORMAT_RGB565
//             temp_fb.format = PIXFORMAT_RGB565;
//             break;
//         case 2:  // PIXFORMAT_RGB888
//             temp_fb.format = PIXFORMAT_RGB888;
//             break;
//         case 3:  // PIXFORMAT_JPEG
//             temp_fb.format = PIXFORMAT_JPEG;
//             break;
//         case 4:  // PIXFORMAT_GRAYSCALE
//             temp_fb.format = PIXFORMAT_GRAYSCALE;
//             break;
//         default:
//             temp_fb.format = PIXFORMAT_RGB565;  // 默认
//             break;
//     }
    
//     // 设置时间戳（使用当前时间）
//     struct timeval tv;
//     gettimeofday(&tv, NULL);
//     temp_fb.timestamp = tv;

//     // 3. 转换为 img_t 格式
//     img_t img = fb_to_img(&temp_fb);
//     ESP_LOGI(TAG_FACE, "Image ready for processing: %dx%d, pixel type: %d", 
//              img.width, img.height, img.pix_type);

//     // 3. 人脸检测
//     ESP_LOGI(TAG_FACE, "Running face detection...");
//     auto det_res = s_face_detect->run(img);

//     // 获取检测到的人脸数量
//     int face_count = 0;
//     for (auto it = det_res.begin(); it != det_res.end(); ++it) {
//         face_count++;
//     }
    
//     ESP_LOGI(TAG_FACE, "Face detection completed. Faces detected: %d", face_count);
    
//     if (face_count == 0) {
//         ESP_LOGW(TAG_FACE, "No face detected!");
        
//         // 释放处理后的帧
//         frame_queue_release_data(frame_data);
//         return;
//     }
    
//     // 显示检测到的人脸信息
//     int face_index = 0;
//     for (auto it = det_res.begin(); it != det_res.end(); ++it, ++face_index) {
//         auto& face = *it;
//         ESP_LOGI(TAG_FACE, "Face %d: Box [%d,%d,%d,%d], Score: %.3f", 
//                  face_index, face.box[0], face.box[1], face.box[2], face.box[3], face.score);
        
//         // 计算人脸在图像中的位置和大小
//         int face_width = face.box[2] - face.box[0];
//         int face_height = face.box[3] - face.box[1];
//         int face_center_x = (face.box[0] + face.box[2]) / 2;
//         int face_center_y = (face.box[1] + face.box[3]) / 2;
        
//         ESP_LOGI(TAG_FACE, "       Size: %dx%d, Center: (%d,%d)", 
//                  face_width, face_height, face_center_x, face_center_y);
//     }
    
//     // 4. 人脸识别（尝试匹配已有的人脸）
//     // bool face_matched = false;
//     // int matched_id = -1;
//     // float matched_similarity = 0.0f;
    
//     // ESP_LOGI(TAG_FACE, "Running face recognition...");
    
//     // // 只有在数据库中有已录入的人脸时才进行识别
//     // if (s_face_count > 0) {
//     //     std::vector<dl::recognition::result_t> res = s_face_recognizer->recognize(img, det_res);
        
//     //     if (!res.empty()) {
//     //         matched_id = res[0].id;
//     //         matched_similarity = res[0].similarity;
            
//     //         ESP_LOGI(TAG_FACE, "Recognition result: ID=%d, Similarity=%.3f", 
//     //                 matched_id, matched_similarity);
            
//     //         // 相似度阈值
//     //         const float SIMILARITY_THRESHOLD = 0.7f;
//     //         if (matched_similarity > SIMILARITY_THRESHOLD) {
//     //             face_matched = true;
//     //             ESP_LOGI(TAG_FACE, "✓ Face MATCHED! (ID: %d)", matched_id);
//     //         } else {
//     //             ESP_LOGI(TAG_FACE, "Face recognized but similarity too low (%.3f <= %.3f)", 
//     //                     matched_similarity, SIMILARITY_THRESHOLD);
//     //         }
//     //     } else {
//     //         ESP_LOGI(TAG_FACE, "No matching face found in database");
//     //     }
//     // } else {
//     //     ESP_LOGI(TAG_FACE, "Database is empty, skipping recognition");
//     // }
    
//     // // 5. 如果未匹配，则录入新的人脸
//     // if (!face_matched) {
//     //     ESP_LOGI(TAG_FACE, "Face not matched, enrolling as new face...");
        
//     //     // 录入新的人脸
//     //     s_face_recognizer->enroll(img, det_res);
        
//     //     // 更新人脸计数
//     //     s_face_count++;
//     //     ESP_LOGI(TAG_FACE, "✓ New face ENROLLED! ID: %d", s_face_count);
//     //     ESP_LOGI(TAG_FACE, "Total faces in database: %d", s_face_count);
//     // } else {
//     //     ESP_LOGI(TAG_FACE, "✓ Face already in database (ID: %d)", matched_id);
//     // }
    
//     // 6. 释放处理后的帧
//     frame_queue_release_data(frame_data);
    
//     ESP_LOGI(TAG_FACE, "=== Face processing completed ===");
// }

static esp_err_t save_rgb565_as_bmp(frame_data_t* frame, const char* filename)
{
    if (!frame || !filename || frame->format != PIXFORMAT_RGB565) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int width = (int)frame->width;
    int height = (int)frame->height;
    
    // BMP文件头大小：54字节 + 颜色表（RGB565不需要颜色表）
    size_t bmp_data_size = width * height * 3;  // RGB888
    size_t bmp_file_size = 54 + bmp_data_size;  // BMP文件头54字节
    
    // 分配内存
    uint8_t* bmp_buffer = (uint8_t*)heap_caps_malloc(bmp_file_size, MALLOC_CAP_SPIRAM);
    if (!bmp_buffer) {
        ESP_LOGE(TAG_FACE, "Failed to allocate BMP buffer");
        return ESP_ERR_NO_MEM;
    }
    
    // 填充BMP文件头
    // BITMAPFILEHEADER (14 bytes)
    bmp_buffer[0] = 'B'; bmp_buffer[1] = 'M';  // 文件类型
    *((uint32_t*)(bmp_buffer + 2)) = bmp_file_size;  // 文件大小
    *((uint32_t*)(bmp_buffer + 6)) = 0;  // 保留
    *((uint32_t*)(bmp_buffer + 10)) = 54;  // 像素数据偏移
    
    // BITMAPINFOHEADER (40 bytes)
    *((uint32_t*)(bmp_buffer + 14)) = 40;  // 信息头大小
    *((int32_t*)(bmp_buffer + 18)) = width;  // 宽度
    *((int32_t*)(bmp_buffer + 22)) = -height;  // 高度（负数表示从上到下）
    *((uint16_t*)(bmp_buffer + 26)) = 1;  // 平面数
    *((uint16_t*)(bmp_buffer + 28)) = 24;  // 每像素位数（24位RGB）
    *((uint32_t*)(bmp_buffer + 30)) = 0;  // 压缩类型（BI_RGB）
    *((uint32_t*)(bmp_buffer + 34)) = bmp_data_size;  // 图像数据大小
    *((int32_t*)(bmp_buffer + 38)) = 0;  // 水平分辨率
    *((int32_t*)(bmp_buffer + 42)) = 0;  // 垂直分辨率
    *((uint32_t*)(bmp_buffer + 46)) = 0;  // 使用的颜色数
    *((uint32_t*)(bmp_buffer + 50)) = 0;  // 重要颜色数
    
    // 转换RGB565到RGB888并填充BMP数据
    uint16_t* rgb565 = (uint16_t*)frame->data;
    uint8_t* rgb888 = bmp_buffer + 54;
    
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint16_t pixel = rgb565[y * width + x];
            
            // 保存图片时需要转换字节序
            pixel = (pixel >> 8) | (pixel << 8);
            
            // 提取RGB565分量
            uint8_t r5 = (pixel >> 11) & 0x1F;
            uint8_t g6 = (pixel >> 5) & 0x3F;
            uint8_t b5 = pixel & 0x1F;
            
            // 扩展为RGB888 (5位扩展到8位: value * 255 / 31)
            // BMP格式是BGR顺序
            *rgb888++ = (b5 * 527 + 23) >> 6;  // B
            *rgb888++ = (g6 * 259 + 33) >> 6;  // G
            *rgb888++ = (r5 * 527 + 23) >> 6;  // R
        }
        // BMP每行需要4字节对齐
        int padding = (4 - (width * 3) % 4) % 4;
        for (int p = 0; p < padding; p++) {
            *rgb888++ = 0;
        }
    }
    
    // 重新计算实际文件大小（因为有对齐填充）
    size_t actual_file_size = 54 + (rgb888 - (bmp_buffer + 54));
    *((uint32_t*)(bmp_buffer + 2)) = actual_file_size;  // 更新文件大小
    
    // 保存BMP文件
    esp_err_t ret = fs_write_file(filename, bmp_buffer, actual_file_size);
    
    heap_caps_free(bmp_buffer);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG_FACE, "✓ BMP face saved successfully (%zu bytes)", actual_file_size);
    } else {
        ESP_LOGE(TAG_FACE, "Failed to save BMP frame: %d", ret);
    }
    
    return ret;
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

        // 从队列获取帧（最多等待100ms）
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

#if SD_CARD
    // 6. 保存最佳帧到SD卡
    if (best_frame_index >= 0 && captured_frames[best_frame_index].frame) {
        frame_data_t* best_frame = captured_frames[best_frame_index].frame;
        
        // 根据原始格式决定保存方式
        if (best_frame->format == PIXFORMAT_JPEG) {  // JPEG格式
            // 直接保存JPEG数据
            char* jpeg_filename = generate_filename("face", "jpg");
            ESP_LOGI(TAG_FACE, "Saving JPEG frame to: %s", jpeg_filename);
            
            esp_err_t ret = fs_write_file(jpeg_filename, 
                                         best_frame->data, 
                                         best_frame->size);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG_FACE, "✓ JPEG face saved successfully (%zu bytes)", best_frame->size);
                
                // 保存元数据信息
                char meta_filename[128];
                snprintf(meta_filename, sizeof(meta_filename), "%s.txt", jpeg_filename);
                
                char meta_info[256];
                snprintf(meta_info, sizeof(meta_info),
                        "Face Detection Info:\n"
                        "Filename: %s\n"
                        "Resolution: %dx%d\n"
                        "Format: JPEG\n"
                        "File Size: %zu bytes\n"
                        "Face Score: %.3f\n"
                        "Detection Time: %lld\n"
                        "Valid Frames: %d/%d\n",
                        jpeg_filename,
                        best_frame->width, best_frame->height,
                        best_frame->size,
                        best_face_score,
                        time(NULL),
                        valid_face_frames, CAPTURE_FRAME_COUNT);
                
                fs_write_file(meta_filename, (uint8_t*)meta_info, strlen(meta_info));
            } else {
                ESP_LOGE(TAG_FACE, "Failed to save JPEG frame: %d", ret);
            }
            
        } else if (best_frame->format == PIXFORMAT_RGB565) {  // RGB565格式
            // 需要转换为JPEG保存（这里简化为直接保存RGB565）
            char* bmp_filename = generate_filename("face", "bmp");
            ESP_LOGI(TAG_FACE, "Converting RGB565 to BMP and saving to: %s", bmp_filename);
            
            esp_err_t ret = save_rgb565_as_bmp(best_frame, bmp_filename);
            
            if (ret == ESP_OK) {
                ESP_LOGI(TAG_FACE, "✓ BMP face saved successfully");
                
                // 保存元数据信息
                char meta_filename[128];
                snprintf(meta_filename, sizeof(meta_filename), "%s.txt", bmp_filename);
                
                char meta_info[256];
                snprintf(meta_info, sizeof(meta_info),
                        "Face Detection Info:\n"
                        "Filename: %s\n"
                        "Resolution: %dx%d\n"
                        "Original Format: RGB565\n"
                        "Saved Format: BMP (24-bit)\n"
                        "Face Score: %.3f\n"
                        "Detection Time: %lld\n"
                        "Valid Frames: %d/%d\n"
                        "Note: BMP files can be viewed directly in Windows\n",
                        bmp_filename,
                        (int)best_frame->width, (int)best_frame->height,
                        best_face_score,
                        time(NULL),
                        valid_face_frames, CAPTURE_FRAME_COUNT);
                
                fs_write_file(meta_filename, (uint8_t*)meta_info, strlen(meta_info));
            } else {
                ESP_LOGE(TAG_FACE, "Failed to save RGB565 frame: %d", ret);
            }
        } else if (best_frame->format == PIXFORMAT_RGB888) {  // RGB888格式
            // 转换为JPEG保存（这里简化为保存原始RGB888）
            // 实际项目中应使用JPEG编码器
            char* rgb_filename = generate_filename("face", "rgb888");
            ESP_LOGI(TAG_FACE, "Saving RGB888 frame to: %s", rgb_filename);
            
            esp_err_t ret = fs_write_file(rgb_filename, 
                                         best_frame->data, 
                                         best_frame->size);
            
            if (ret == ESP_OK) {
                ESP_LOGI(TAG_FACE, "✓ RGB888 face saved successfully (%zu bytes)", best_frame->size);
                
                // 保存格式信息
                char info_filename[128];
                snprintf(info_filename, sizeof(info_filename), "%s.txt", rgb_filename);
                
                char format_info[256];
                snprintf(format_info, sizeof(format_info),
                        "RGB888 Raw Image Info:\n"
                        "Width: %d\n"
                        "Height: %d\n"
                        "Pixel Format: RGB888 (24-bit)\n"
                        "File Size: %zu bytes\n"
                        "Face Score: %.3f\n",
                        best_frame->width, best_frame->height,
                        best_frame->size,
                        best_face_score);
                
                fs_write_file(info_filename, (uint8_t*)format_info, strlen(format_info));
            } else {
                ESP_LOGE(TAG_FACE, "Failed to save RGB888 frame: %d", ret);
            }
        } else {
            ESP_LOGW(TAG_FACE, "Unsupported frame format: %d", best_frame->format);
        }
    }
#endif

    // 7. 释放所有帧资源
    free_capture_frames(captured_frames, CAPTURE_FRAME_COUNT);
}

// 新增辅助函数：检查SD卡状态
static bool check_sd_card_status(void)
{
    if (!fs_is_mounted("/sdcard")) {
        ESP_LOGW(TAG_FACE, "SD card not mounted, attempting to mount...");
        esp_err_t ret = fs_sd_card_init("/sdcard");
        if (ret != ESP_OK) {
            ESP_LOGE(TAG_FACE, "Failed to mount SD card: %d", ret);
            return false;
        }
    }
    return true;
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

            process_face_detect();
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
        16384,  // 增加栈大小
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