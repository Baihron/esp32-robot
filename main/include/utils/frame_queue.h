#ifndef FRAME_QUEUE_H
#define FRAME_QUEUE_H

#include "esp_camera.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// 帧数据格式
typedef struct {
    uint8_t *data;          // 帧数据指针
    size_t size;            // 数据大小
    size_t max_size;        // 缓冲区最大容量
    uint16_t width;         // 图像宽度
    uint16_t height;        // 图像高度
    pixformat_t format;     // 图像格式
    TickType_t timestamp;   // 时间戳
    uint32_t copy_time;     // 拷贝耗时（ms）
    uint8_t in_use;         // 是否在使用中（0/1）
} frame_data_t;

/**
 * @brief 初始化帧队列和缓冲区池
 * @param queue_size 队列大小（最多缓存的帧数）
 * @param max_frame_size 每帧的最大字节数
 * @return esp_err_t 错误码
 */
esp_err_t frame_queue_init(uint32_t queue_size, size_t max_frame_size);

/**
 * @brief 拷贝摄像头帧到队列（使用缓冲区池）
 * @param fb 摄像头帧缓冲区
 * @param timeout 超时时间
 * @return esp_err_t 错误码
 */
esp_err_t frame_queue_send_copy(camera_fb_t *fb, TickType_t timeout);

/**
 * @brief 从队列接收帧数据
 * @param timeout 超时时间
 * @return frame_data_t* 帧数据指针，使用后必须调用frame_queue_release_data释放
 */
frame_data_t *frame_queue_receive_data(TickType_t timeout);

/**
 * @brief 释放帧数据缓冲区（重要！必须调用）
 * @param frame 帧数据指针
 */
void frame_queue_release_data(frame_data_t *frame);

/**
 * @brief 获取队列中待处理的帧数量
 * @return UBaseType_t 帧数量
 */
UBaseType_t frame_queue_count(void);

/**
 * @brief 获取空闲帧缓冲区数量
 * @return UBaseType_t 空闲缓冲区数量
 */
UBaseType_t frame_queue_free_count(void);

/**
 * @brief 清空帧队列
 */
void frame_queue_clear(void);

/**
 * @brief 反初始化帧队列
 */
void frame_queue_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // FRAME_QUEUE_H