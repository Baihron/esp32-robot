#ifndef VOICE_CHAT_CLIENT_H
#define VOICE_CHAT_CLIENT_H

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==================== 百度端到端语音大模型配置 ====================
// 请到百度智能云控制台创建应用，获取以下信息
#define BAIDU_API_KEY           "ZhkTeZe0DJXyLJUEmwBOlNXP"       // TODO: 填入你的 API Key
#define BAIDU_SECRET_KEY        "7R3xzaCxe3K5kxNYDE1w255lz4AyiYQl"     // TODO: 填入你的 Secret Key
#define BAIDU_APP_ID            "123064657"         // TODO: 填入你的 App ID

// 模型选择 (一般不用改)
#define BAIDU_MODEL             "audio-realtime-near" // Pro版近场模型

// ==================== 音频参数 (保持不变) ====================
#define SAMPLE_RATE             16000
#define SAMPLE_BITS             16
#define CHANNELS                1
#define AUDIO_FRAME_MS          160            // 每帧160ms (百度推荐)
#define SAMPLES_PER_FRAME       (SAMPLE_RATE * AUDIO_FRAME_MS / 1000)   // 2560
#define AUDIO_FRAME_BYTES       (SAMPLES_PER_FRAME * SAMPLE_BITS / 8)   // 5120

/**
 * @brief 初始化语音客户端 (获取token、建立websocket连接)
 * @return ESP_OK 成功，否则失败
 */
esp_err_t voice_chat_client_init(void);

/**
 * @brief 发送音频并等待回复
 * @param pcm_audio 输入的PCM音频数据 (int16_t, 小端)
 * @param pcm_bytes 输入音频的字节数
 * @param reply_buf 用于接收输出音频的缓冲区
 * @param reply_len 输出音频的实际字节数
 * @param max_reply_len reply_buf 的最大大小
 * @return ESP_OK 成功，否则失败
 */
esp_err_t voice_chat_client_send_audio(
    const int16_t *pcm_audio,
    size_t pcm_bytes,
    int16_t *reply_buf,
    size_t *reply_len,
    size_t max_reply_len);

/**
 * @brief 释放所有资源
 */
void voice_chat_client_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // VOICE_CHAT_CLIENT_H