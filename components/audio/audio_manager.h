// components/audio/audio_manager.h
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化音频管理器（创建 AudioService 实例并初始化）
 * @return ESP_OK 成功，其他失败
 */
esp_err_t audio_manager_init(void);

/**
 * @brief 启动音频服务（开始采集、编解码、处理唤醒词等）
 * @return ESP_OK 成功，其他失败
 */
esp_err_t audio_manager_start(void);

/**
 * @brief 停止音频服务
 * @return ESP_OK 成功，其他失败
 */
esp_err_t audio_manager_stop(void);

/**
 * @brief 检查音频服务是否正在运行
 */
bool audio_manager_is_running(void);

/**
 * @brief 获取音频服务发送队列中的一个音频包（需要外部协议发送）
 * @return 音频包指针，若无数据则返回 nullptr
 */
void* audio_manager_pop_send_packet(void);  // 实际返回 std::unique_ptr<AudioStreamPacket>，但为了 C 接口，返回 void*

/**
 * @brief 将从服务器收到的音频包推入音频服务解码队列
 * @param data 音频数据指针
 * @param size 数据大小
 * @param sample_rate 采样率
 * @param frame_duration 帧时长（ms）
 * @param timestamp 时间戳（可选）
 * @return true 成功，false 失败（队列满）
 */
bool audio_manager_push_decode_packet(const uint8_t* data, size_t size, int sample_rate, int frame_duration, uint32_t timestamp);

#ifdef __cplusplus
}
#endif