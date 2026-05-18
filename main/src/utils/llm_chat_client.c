#include "llm_chat_client.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "LLM_CLIENT";

// 请替换为你自己的 DeepSeek API Key
// #define DEEPSEEK_API_KEY     "你的DeepSeek_API_Key"
#define DEEPSEEK_API_URL     "https://api.deepseek.com/chat/completions"

// HTTP 响应缓冲区
static char g_llm_response_buf[4096] = {0};
static size_t g_llm_response_len = 0;

// HTTP 事件回调
static esp_err_t llm_http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (evt->data_len > 0 &&
            g_llm_response_len + evt->data_len < sizeof(g_llm_response_buf)) {
            memcpy(g_llm_response_buf + g_llm_response_len, evt->data, evt->data_len);
            g_llm_response_len += evt->data_len;
            g_llm_response_buf[g_llm_response_len] = '\0';
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        g_llm_response_buf[g_llm_response_len] = '\0';
        ESP_LOGI(TAG, "LLM Response: %s", g_llm_response_buf);
        break;
    default:
        break;
    }
    return ESP_OK;
}

/**
 * @brief 对 JSON 字符串值进行转义：双引号、反斜杠、控制字符
 */
static void json_escape(const char *src, char *dst, size_t dst_size)
{
    size_t i = 0, j = 0;
    while (src[i] != '\0' && j < dst_size - 1) {
        switch (src[i]) {
        case '"':
            if (j + 1 < dst_size - 1) { dst[j++] = '\\'; dst[j++] = '"'; }
            else { dst[j] = '\0'; return; }
            break;
        case '\\':
            if (j + 1 < dst_size - 1) { dst[j++] = '\\'; dst[j++] = '\\'; }
            else { dst[j] = '\0'; return; }
            break;
        case '\n':
            if (j + 1 < dst_size - 1) { dst[j++] = '\\'; dst[j++] = 'n'; }
            else { dst[j] = '\0'; return; }
            break;
        case '\r':
            if (j + 1 < dst_size - 1) { dst[j++] = '\\'; dst[j++] = 'r'; }
            else { dst[j] = '\0'; return; }
            break;
        case '\t':
            if (j + 1 < dst_size - 1) { dst[j++] = '\\'; dst[j++] = 't'; }
            else { dst[j] = '\0'; return; }
            break;
        default:
            if (src[i] < 0x20) {
                // 其他控制字符转为空白（或可忽略），这里简单替换为空格
                dst[j++] = ' ';
            } else {
                dst[j++] = src[i];
            }
            break;
        }
        i++;
    }
    dst[j] = '\0';
}

esp_err_t llm_chat_get_emotion(const char *user_text,
                               char *emotion_out,
                               size_t out_size)
{
    if (!user_text || !emotion_out || out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // 1. 系统提示词（未转义原始版）
    const char *system_prompt_raw =
        "你是一个情感判断机器人。根据用户的输入，从以下表情列表中选择最恰当的一个：\n"
        "可用表情：happy（开心），sad（难过），angry（愤怒），surprised（惊讶），"
        "sleepy（困倦），loving（爱心），confused（困惑）,laughing（大笑）,neutral（中性）\n"
        "你必须只回答一个表情单词，不要添加任何其他字符或标点。\n"
        "neutral（中性）是一个特殊的返回信号，只有当你完全无法判断出应该要表达什么心情时才可以返回neutral（中性）。\n"
        "例如：用户说“我今天好开心”，你回答“happy”。";

    // 2. 转义系统提示词和用户文本
    char escaped_system[1024];
    json_escape(system_prompt_raw, escaped_system, sizeof(escaped_system));
    char escaped_user[512];
    json_escape(user_text, escaped_user, sizeof(escaped_user));

    // 3. 构建 JSON 请求体
    char request_body[3072];
    int n = snprintf(request_body, sizeof(request_body),
        "{"
        "\"model\": \"deepseek-chat\","
        "\"messages\": ["
        "{\"role\": \"system\", \"content\": \"%s\"},"
        "{\"role\": \"user\", \"content\": \"%s\"}"
        "],"
        "\"temperature\": 0.3,"
        "\"max_tokens\": 10"
        "}", escaped_system, escaped_user);

    if (n < 0 || n >= sizeof(request_body)) {
        ESP_LOGE(TAG, "Request body truncated");
        return ESP_FAIL;
    }

    // 4. HTTP 客户端配置
    esp_http_client_config_t config = {
        .url = DEEPSEEK_API_URL,
        .method = HTTP_METHOD_POST,
        .event_handler = llm_http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .buffer_size = 8192,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return ESP_FAIL;
    }

    char auth_header[128];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", DEEPSEEK_API_KEY);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, request_body, strlen(request_body));

    memset(g_llm_response_buf, 0, sizeof(g_llm_response_buf));
    g_llm_response_len = 0;

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP status: %d", status_code);
    esp_http_client_cleanup(client);

    if (status_code != 200) {
        ESP_LOGE(TAG, "Error response: %s", g_llm_response_buf);
        return ESP_FAIL;
    }

    // 5. 解析响应
    cJSON *root = cJSON_Parse(g_llm_response_buf);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse error");
        return ESP_FAIL;
    }

    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (!choices || !cJSON_IsArray(choices)) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "No 'choices' array");
        return ESP_FAIL;
    }

    cJSON *first_choice = cJSON_GetArrayItem(choices, 0);
    if (!first_choice) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Empty choices array");
        return ESP_FAIL;
    }

    cJSON *message = cJSON_GetObjectItem(first_choice, "message");
    if (!message) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "No 'message' object");
        return ESP_FAIL;
    }

    cJSON *content = cJSON_GetObjectItem(message, "content");
    if (!content || !cJSON_IsString(content)) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "No 'content' string");
        return ESP_FAIL;
    }

    strncpy(emotion_out, content->valuestring, out_size - 1);
    emotion_out[out_size - 1] = '\0';

    // 去除首位空白
    char *start = emotion_out;
    while (*start == ' ' || *start == '\t' || *start == '\n') start++;
    char *end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\n')) end--;
    *(end + 1) = '\0';
    if (start != emotion_out) {
        memmove(emotion_out, start, strlen(start) + 1);
    }

    ESP_LOGI(TAG, "Emotion result: %s", emotion_out);
    cJSON_Delete(root);
    return ESP_OK;
}