// voice_chat_client.h
#ifndef VOICE_CHAT_CLIENT_H
#define VOICE_CHAT_CLIENT_H

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==================== 百度语音识别配置 ====================
#define BAIDU_API_KEY           "ZhkTeZe0DJXyLJUEmwBOlNXP"       // 你的 API Key
#define BAIDU_SECRET_KEY        "7R3xzaCxe3K5kxNYDE1w255lz4AyiYQl" // 你的 Secret Key
#define BAIDU_APP_ID            "123064657"                     // 你的 App ID

// 短语音识别标准版 API 地址 (使用 access_token)
#define BAIDU_ASR_URL_FORMAT    "http://vop.baidu.com/server_api?cuid=esp32&token=%s&dev_pid=1537"

// ==================== 音频参数 ====================
#define SAMPLE_RATE             16000
#define SAMPLE_BITS             16
#define CHANNELS                1

esp_err_t voice_chat_client_init(void);

esp_err_t voice_chat_client_send_audio(
    const int16_t *pcm_audio,
    size_t pcm_bytes,
    char *result_text,
    size_t result_size);

void voice_chat_client_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // VOICE_CHAT_CLIENT_H