#include "frame_queue.h"
#include "esp_log.h"
#include "config.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "FRAME_QUEUE";

// 全局队列句柄
static QueueHandle_t g_frame_queue = NULL;

// 缓冲区池 - 预分配固定数量的帧缓冲区
static frame_data_t* g_frame_pool[DEFAULT_FRAME_QUEUE_SIZE] = {NULL};
static uint8_t* g_buffer_pool[DEFAULT_FRAME_QUEUE_SIZE] = {NULL};
static QueueHandle_t g_free_frames = NULL;

// 初始化帧队列和缓冲区池
esp_err_t frame_queue_init(uint32_t queue_size, size_t max_frame_size)
{
    if (g_frame_queue != NULL) {
        ESP_LOGW(TAG, "Frame queue already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 创建帧队列 - 存储 frame_data_t* 指针
    g_frame_queue = xQueueCreate(queue_size, sizeof(frame_data_t *));
    if (g_frame_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create frame queue");
        return ESP_ERR_NO_MEM;
    }
    
    // 创建空闲帧队列 - 管理可用的帧缓冲区
    g_free_frames = xQueueCreate(DEFAULT_FRAME_QUEUE_SIZE, sizeof(frame_data_t *));
    if (g_free_frames == NULL) {
        vQueueDelete(g_frame_queue);
        g_frame_queue = NULL;
        ESP_LOGE(TAG, "Failed to create free frames queue");
        return ESP_ERR_NO_MEM;
    }
    
    // 预分配所有帧缓冲区和数据缓冲区
    ESP_LOGI(TAG, "Allocating frame pool: %d frames, %d bytes each", DEFAULT_FRAME_QUEUE_SIZE, max_frame_size);
    
    for (int i = 0; i < DEFAULT_FRAME_QUEUE_SIZE; i++) {
        // 分配帧数据结构
        g_frame_pool[i] = heap_caps_malloc(sizeof(frame_data_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (g_frame_pool[i] == NULL) {
            ESP_LOGE(TAG, "Failed to allocate frame data structure %d", i);
            goto cleanup;
        }
        
        // 分配数据缓冲区
        g_buffer_pool[i] = heap_caps_malloc(max_frame_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (g_buffer_pool[i] == NULL) {
            ESP_LOGE(TAG, "Failed to allocate frame buffer %d (%d bytes)", i, max_frame_size);
            free(g_frame_pool[i]);
            g_frame_pool[i] = NULL;
            goto cleanup;
        }
        
        // 初始化帧数据
        g_frame_pool[i]->data = g_buffer_pool[i];
        g_frame_pool[i]->max_size = max_frame_size;
        g_frame_pool[i]->in_use = 0;  // 初始标记为未使用
        
        // 将帧放入空闲队列
        BaseType_t result = xQueueSend(g_free_frames, &g_frame_pool[i], 0);
        if (result != pdPASS) {
            ESP_LOGE(TAG, "Failed to add frame %d to free queue", i);
            free(g_buffer_pool[i]);
            free(g_frame_pool[i]);
            g_buffer_pool[i] = NULL;
            g_frame_pool[i] = NULL;
        }
    }
    
    ESP_LOGI(TAG, "Frame queue initialized: size=%d, pool=%d, max_frame=%d bytes", 
             queue_size, DEFAULT_FRAME_QUEUE_SIZE, max_frame_size);
    return ESP_OK;

cleanup:
    // 清理已分配的资源
    for (int i = 0; i < DEFAULT_FRAME_QUEUE_SIZE; i++) {
        if (g_buffer_pool[i]) {
            free(g_buffer_pool[i]);
            g_buffer_pool[i] = NULL;
        }
        if (g_frame_pool[i]) {
            free(g_frame_pool[i]);
            g_frame_pool[i] = NULL;
        }
    }
    
    if (g_free_frames) {
        vQueueDelete(g_free_frames);
        g_free_frames = NULL;
    }
    
    if (g_frame_queue) {
        vQueueDelete(g_frame_queue);
        g_frame_queue = NULL;
    }
    
    return ESP_ERR_NO_MEM;
}

// 获取一个空闲帧缓冲区
static frame_data_t* get_free_frame(TickType_t timeout)
{
    if (g_free_frames == NULL) {
        return NULL;
    }
    
    frame_data_t* frame = NULL;
    BaseType_t result = xQueueReceive(g_free_frames, &frame, timeout);
    
    if (result == pdPASS && frame != NULL) {
        frame->in_use = 1;  // 标记为使用中
        return frame;
    }
    
    return NULL;
}

// 归还帧缓冲区到空闲池
static void return_free_frame(frame_data_t* frame)
{
    if (frame && g_free_frames) {
        frame->in_use = 0;  // 标记为未使用
        frame->size = 0;    // 清空大小
        
        // 注意：这里不清除data内容，避免不必要的memset开销
        // 下次使用时直接覆盖即可
        
        BaseType_t result = xQueueSend(g_free_frames, &frame, 0);
        if (result != pdPASS) {
            ESP_LOGW(TAG, "Failed to return frame to free queue");
        }
    }
}

// 发送帧到队列（使用缓冲区池）
esp_err_t frame_queue_send_copy(camera_fb_t *fb, TickType_t timeout)
{
    if (g_frame_queue == NULL) {
        ESP_LOGE(TAG, "Frame queue not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (fb == NULL || fb->buf == NULL) {
        ESP_LOGE(TAG, "Invalid frame pointer");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (fb->len == 0) {
        ESP_LOGE(TAG, "Frame data length is 0");
        return ESP_ERR_INVALID_SIZE;
    }
    
    // 1. 从空闲池获取一个帧缓冲区
    frame_data_t *frame_data = get_free_frame(timeout);
    if (frame_data == NULL) {
        ESP_LOGW(TAG, "No free frame buffer available");
        return ESP_ERR_TIMEOUT;
    }
    
    // 2. 检查缓冲区大小是否足够
    if (fb->len > frame_data->max_size) {
        ESP_LOGE(TAG, "Frame too large: %d > %d", fb->len, frame_data->max_size);
        return_free_frame(frame_data);
        return ESP_ERR_INVALID_SIZE;
    }
    
    // 3. 拷贝数据（必要的拷贝！）
    uint32_t start_time = xTaskGetTickCount();
    memcpy(frame_data->data, fb->buf, fb->len);
    uint32_t copy_time = xTaskGetTickCount() - start_time;
    
    // 4. 填充帧信息
    frame_data->size = fb->len;
    frame_data->width = fb->width;
    frame_data->height = fb->height;
    frame_data->format = fb->format;
    frame_data->timestamp = xTaskGetTickCount();
    frame_data->copy_time = copy_time;  // 记录拷贝耗时（用于性能分析）
    
    // 5. 发送到帧队列
    BaseType_t result = xQueueSend(g_frame_queue, &frame_data, timeout);
    if (result != pdPASS) {
        ESP_LOGW(TAG, "Frame queue full or timeout");
        return_free_frame(frame_data);  // 发送失败，归还缓冲区
        return ESP_ERR_TIMEOUT;
    }

    // ESP_LOGI(TAG, "Frame queued: %dx%d, %d bytes, copy_time=%dms", fb->width, fb->height, fb->len, copy_time);

    return ESP_OK;
}

// 从队列接收帧数据（自动管理缓冲区）
frame_data_t *frame_queue_receive_data(TickType_t timeout)
{
    if (g_frame_queue == NULL) {
        ESP_LOGE(TAG, "Frame queue not initialized");
        return NULL;
    }
    
    frame_data_t *frame_data = NULL;
    BaseType_t result = xQueueReceive(g_frame_queue, &frame_data, timeout);
    
    if (result != pdPASS) {
        return NULL;  // 超时或队列空
    }
    
    return frame_data;
}

// 使用完帧数据后，必须调用此函数释放缓冲区
void frame_queue_release_data(frame_data_t *frame)
{
    if (frame) {
        return_free_frame(frame);
    }
}

// 获取队列中待处理的帧数量
UBaseType_t frame_queue_count(void)
{
    if (g_frame_queue == NULL) {
        return 0;
    }
    
    return uxQueueMessagesWaiting(g_frame_queue);
}

// 获取空闲帧缓冲区数量
UBaseType_t frame_queue_free_count(void)
{
    if (g_free_frames == NULL) {
        return 0;
    }
    
    return uxQueueMessagesWaiting(g_free_frames);
}

// 清空帧队列
void frame_queue_clear(void)
{
    if (g_frame_queue == NULL) {
        return;
    }
    
    frame_data_t *frame_data = NULL;
    while (xQueueReceive(g_frame_queue, &frame_data, 0) == pdPASS) {
        if (frame_data) {
            return_free_frame(frame_data);
        }
    }
    
    ESP_LOGI(TAG, "Frame queue cleared");
}

// 反初始化
void frame_queue_deinit(void)
{
    // 先清空队列
    frame_queue_clear();
    
    // 删除队列
    if (g_frame_queue != NULL) {
        vQueueDelete(g_frame_queue);
        g_frame_queue = NULL;
    }
    
    if (g_free_frames != NULL) {
        vQueueDelete(g_free_frames);
        g_free_frames = NULL;
    }
    
    // 释放所有预分配的缓冲区
    for (int i = 0; i < DEFAULT_FRAME_QUEUE_SIZE; i++) {
        if (g_buffer_pool[i]) {
            free(g_buffer_pool[i]);
            g_buffer_pool[i] = NULL;
        }
        if (g_frame_pool[i]) {
            free(g_frame_pool[i]);
            g_frame_pool[i] = NULL;
        }
    }
    
    ESP_LOGI(TAG, "Frame queue deinitialized");
}