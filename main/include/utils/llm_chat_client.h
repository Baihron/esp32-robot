#ifndef LLM_CHAT_CLIENT_H
#define LLM_CHAT_CLIENT_H

#include "esp_err.h"
#include <stddef.h>

#define DEEPSEEK_API_KEY     "sk-6102e1cb8e6b4b32a56fdf1644ca1623"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 将用户输入文本发送给大模型，获取机器人应展示的表情名称
 * @param user_text  用户说话文本（ASR结果）
 * @param emotion_out 输出缓冲区，存放表情标识符（如 "happy"）
 * @param out_size   输出缓冲区大小
 * @return ESP_OK 成功，否则失败
 */
esp_err_t llm_chat_get_emotion(const char *user_text,
                               char *emotion_out,
                               size_t out_size);

#ifdef __cplusplus
}
#endif

#endif // LLM_CHAT_CLIENT_H