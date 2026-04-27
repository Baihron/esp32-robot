#include "voice_chat_client.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include "mbedtls/base64.h"
#include "esp_crt_bundle.h"

static const char *TAG = "BAIDU_LLM";

// ==================== 固定配置 ====================
#define BAIDU_TOKEN_URL        "https://aip.baidubce.com/oauth/2.0/token"
#define BAIDU_WS_URL_BASE     "wss://aip.baidubce.com/ws/2.0/speech/v1/realtime"

// ==================== 全局状态 ====================
static esp_websocket_client_handle_t g_ws = NULL;
static bool g_connected = false;
static char g_access_token[256] = {0};          // token 最长 256
static char g_full_url[512] = {0};

// 接收缓冲区
static int16_t *g_recv_buf = NULL;
static size_t g_recv_len = 0;
static size_t g_recv_max = 0;
static bool g_recv_done = false;
static TaskHandle_t g_wait_task = NULL;

// ==================== 工具函数 ====================

/**
 * @brief 获取百度 access_token
 * 需要填入 BAIDU_API_KEY 和 BAIDU_SECRET_KEY
 */
static esp_err_t baidu_get_access_token(void) {
    if (g_access_token[0] != '\0') return ESP_OK;

    char post_data[512];
    snprintf(post_data, sizeof(post_data),
             "grant_type=client_credentials&client_id=%s&client_secret=%s",
             BAIDU_API_KEY, BAIDU_SECRET_KEY);

    esp_http_client_config_t config = {
        .url = BAIDU_TOKEN_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    // 关键：让服务器关闭连接，便于我们读取到 EOF
    esp_http_client_set_header(client, "Connection", "close");

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ HTTP请求失败: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "📡 Token请求 HTTP状态码: %d", status);

    // 动态分配缓冲区，循环读取直到读完所有数据
    size_t buf_size = 1024;
    char *response = malloc(buf_size);
    if (!response) {
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    int total_read = 0;
    while (1) {
        if (total_read + 1 >= buf_size) {
            buf_size *= 2;
            char *tmp = realloc(response, buf_size);
            if (!tmp) {
                free(response);
                esp_http_client_cleanup(client);
                return ESP_ERR_NO_MEM;
            }
            response = tmp;
        }
        int read_len = esp_http_client_read(client, response + total_read, buf_size - total_read - 1);
        if (read_len <= 0) {
            break;      // 读取结束或出错
        }
        total_read += read_len;
    }

    response[total_read] = '\0';
    ESP_LOGI(TAG, "📡 读取响应字节数: %d", total_read);
    if (total_read > 0) {
        ESP_LOGI(TAG, "📡 服务器响应: %s", response);
    } else {
        ESP_LOGW(TAG, "📡 服务器返回空响应体");
    }

    if (status != 200) {
        ESP_LOGE(TAG, "❌ 获取 token 失败 (HTTP %d)", status);
        free(response);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(response);
    if (root) {
        cJSON *token = cJSON_GetObjectItem(root, "access_token");
        if (token && cJSON_IsString(token)) {
            strncpy(g_access_token, token->valuestring, sizeof(g_access_token) - 1);
            g_access_token[sizeof(g_access_token) - 1] = '\0';
            ESP_LOGI(TAG, "✅ 获取 access_token 成功");
        } else {
            ESP_LOGE(TAG, "❌ 响应中没有 access_token");
            err = ESP_FAIL;
        }
        cJSON_Delete(root);
    } else {
        ESP_LOGE(TAG, "❌ JSON解析失败: %s", response);
        err = ESP_FAIL;
    }

    free(response);
    esp_http_client_cleanup(client);
    return (g_access_token[0] != '\0') ? ESP_OK : ESP_FAIL;
}

/* base64 编码 */
static int base64_encode(const uint8_t *src, size_t slen, char *dst, size_t dlen) {
    size_t olen;
    int ret = mbedtls_base64_encode((unsigned char *)dst, dlen, &olen, src, slen);
    return (ret == 0) ? (int)olen : -1;
}

/* base64 解码 */
static int base64_decode(const char *src, size_t slen, uint8_t *dst, size_t dlen) {
    size_t olen;
    int ret = mbedtls_base64_decode((unsigned char *)dst, dlen, &olen, (const unsigned char *)src, slen);
    return (ret == 0) ? (int)olen : -1;
}

// ==================== WebSocket 发送函数 ====================

static esp_err_t ws_send_text(const char *json) {
    if (!g_connected) return ESP_ERR_INVALID_STATE;
    int ret = esp_websocket_client_send_text(g_ws, json, strlen(json), pdMS_TO_TICKS(3000));
    return ret < 0 ? ESP_FAIL : ESP_OK;
}

static esp_err_t ws_send_bin(const uint8_t *data, size_t len) {
    if (!g_connected) return ESP_ERR_INVALID_STATE;
    int ret = esp_websocket_client_send_bin(g_ws, (const char *)data, len, pdMS_TO_TICKS(3000));
    return ret < 0 ? ESP_FAIL : ESP_OK;
}

// ==================== WebSocket 事件处理 ====================

static void ws_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    esp_websocket_event_data_t *ed = (esp_websocket_event_data_t *)data;

    if (id == WEBSOCKET_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "✅ WebSocket 已连接");
        g_connected = true;
    }
    else if (id == WEBSOCKET_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "❌ 断开连接");
        g_connected = false;
        // 如果还在等待回复，提前唤醒
        if (g_wait_task) {
            g_recv_done = true;
            xTaskNotifyGive(g_wait_task);
        }
    }
    else if (id == WEBSOCKET_EVENT_DATA && ed->op_code == 0x2) {
        // 二进制帧 (本接口一般不会用到，保留)
        ESP_LOGW(TAG, "⚠️ 收到意外的二进制帧, length=%d", ed->data_len);
    }
    else if (id == WEBSOCKET_EVENT_DATA && ed->op_code == 0x1) {
        // 文本JSON事件
        char json[2048];
        size_t cpy_len = ed->data_len < sizeof(json) - 1 ? ed->data_len : sizeof(json) - 1;
        memcpy(json, ed->data_ptr, cpy_len);
        json[cpy_len] = '\0';
        ESP_LOGD(TAG, "📩 收到事件: %s", json);

        cJSON *root = cJSON_Parse(json);
        if (!root) return;

        cJSON *type = cJSON_GetObjectItem(root, "type");
        if (!type || !cJSON_IsString(type)) {
            cJSON_Delete(root);
            return;
        }

        const char *event = type->valuestring;

        if (strcmp(event, "session.created") == 0) {
            // 会话已创建，可以发送音频了
            ESP_LOGI(TAG, "✅ 会话已创建");
        }
        else if (strcmp(event, "response.audio.delta") == 0) {
            // 收到一段合成语音 (base64)
            cJSON *delta = cJSON_GetObjectItem(root, "delta");
            if (delta && cJSON_IsString(delta)) {
                const char *b64 = delta->valuestring;
                size_t b64_len = strlen(b64);
                // 估算解码后大小
                size_t dec_len = (b64_len * 3) / 4 + 4;
                if (g_recv_buf && g_recv_len + dec_len <= g_recv_max) {
                    int written = base64_decode(b64, b64_len,
                                               (uint8_t *)(g_recv_buf) + g_recv_len,
                                               g_recv_max - g_recv_len);
                    if (written > 0) {
                        g_recv_len += written;
                        ESP_LOGD(TAG, "🎵 收到音频片段, size=%d, total=%zu", written, g_recv_len);
                    } else {
                        ESP_LOGE(TAG, "❌ base64解码失败");
                    }
                } else {
                    ESP_LOGE(TAG, "❌ 接收缓冲区不足");
                }
            }
        }
        else if (strcmp(event, "response.done") == 0) {
            ESP_LOGI(TAG, "✅ 本轮对话完成");
            g_recv_done = true;
            if (g_wait_task) {
                xTaskNotifyGive(g_wait_task);
            }
        }
        else if (strcmp(event, "error") == 0) {
            // 处理错误
            cJSON *error = cJSON_GetObjectItem(root, "error");
            if (error) {
                char *err_str = cJSON_PrintUnformatted(error);
                ESP_LOGE(TAG, "❌ 服务端错误: %s", err_str);
                free(err_str);
            }
            g_recv_done = true;
            if (g_wait_task) {
                xTaskNotifyGive(g_wait_task);
            }
        }
        // 其他事件可以忽略

        cJSON_Delete(root);
    }
}

// ==================== 公共 API ====================

esp_err_t voice_chat_client_init(void) {
    if (g_ws) return ESP_OK;

    // 1. 获取 access token
    esp_err_t ret = baidu_get_access_token();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ 获取 access token 失败");
        return ret;
    }

    // 2. 拼接 WebSocket URL
    snprintf(g_full_url, sizeof(g_full_url),
             "%s?model=%s&access_token=%s",
             BAIDU_WS_URL_BASE, BAIDU_MODEL, g_access_token);

    esp_websocket_client_config_t cfg = {
        .uri = g_full_url,
        .task_prio = tskIDLE_PRIORITY + 5,
        .task_stack = 16384,
        .buffer_size = 8192,
        .network_timeout_ms = 5000,
        .reconnect_timeout_ms = 0,
        .skip_cert_common_name_check = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    g_ws = esp_websocket_client_init(&cfg);
    if (!g_ws) {
        ESP_LOGE(TAG, "❌ WebSocket 客户端初始化失败");
        return ESP_FAIL;
    }

    esp_websocket_register_events(g_ws, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL);
    esp_websocket_client_start(g_ws);

    // 3. 等待连接
    int timeout = 0;
    while (!g_connected && timeout < 50) {
        vTaskDelay(pdMS_TO_TICKS(100));
        timeout++;
    }
    if (!g_connected) {
        ESP_LOGE(TAG, "❌ WebSocket 连接超时");
        voice_chat_client_deinit();
        return ESP_ERR_TIMEOUT;
    }

    // 4. 发送 session.update (配置 VAD 等，可选)
    const char *session_update =
        "{"
        "\"type\":\"session.update\","
        "\"session\":{"
            "\"turn_detection\":{"
                "\"type\":\"server_vad\","
                "\"create_response\":true,"
                "\"interrupt_response\":true"
            "}"
        "}"
        "}";
    ws_send_text(session_update);

    // 5. 等待 session.updated (短暂延时)
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "✅ 百度语音客户端初始化完成");
    return ESP_OK;
}

esp_err_t voice_chat_client_send_audio(
    const int16_t *pcm_audio,
    size_t pcm_bytes,
    int16_t *reply_buf,
    size_t *reply_len,
    size_t max_reply_len)
{
    *reply_len = 0;
    if (!g_connected) return ESP_ERR_INVALID_STATE;

    // 设置接收缓冲区
    g_recv_buf = reply_buf;
    g_recv_len = 0;
    g_recv_max = max_reply_len;
    g_recv_done = false;
    g_wait_task = xTaskGetCurrentTaskHandle();

    // 1. 分片发送音频数据 (通过 JSON base64)
    size_t offset = 0;
    while (offset < pcm_bytes) {
        size_t chunk_size = (pcm_bytes - offset) > AUDIO_FRAME_BYTES ? AUDIO_FRAME_BYTES : (pcm_bytes - offset);
        
        // base64 编码
        size_t b64_needed = 4 * ((chunk_size + 2) / 3) + 1; // 估算
        char *b64_buf = malloc(b64_needed);
        if (!b64_buf) {
            ESP_LOGE(TAG, "❌ 内存不足");
            return ESP_ERR_NO_MEM;
        }
        int b64_len = base64_encode((const uint8_t *)pcm_audio + offset, chunk_size, b64_buf, b64_needed);
        if (b64_len < 0) {
            ESP_LOGE(TAG, "❌ base64编码失败");
            free(b64_buf);
            return ESP_FAIL;
        }

        // 构建 JSON 消息
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "type", "input_audio_buffer.append");
        cJSON_AddStringToObject(root, "audio", b64_buf);
        char *json_str = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        free(b64_buf);

        // 发送
        esp_err_t ret = ws_send_text(json_str);
        free(json_str);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "❌ 发送音频帧失败");
            return ret;
        }

        offset += chunk_size;
        vTaskDelay(pdMS_TO_TICKS(AUDIO_FRAME_MS)); // 控制发送速率
    }

    // 2. 提交音频
    ws_send_text("{\"type\":\"input_audio_buffer.commit\"}");

    // 3. 请求合成回复
    ws_send_text("{\"type\":\"response.create\"}");

    // 4. 等待回复 (最多15秒)
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(15000));
    
    *reply_len = g_recv_len;
    g_wait_task = NULL;

    ESP_LOGI(TAG, "✅ 接收完成：%zu 字节", *reply_len);
    return ESP_OK;
}

void voice_chat_client_deinit(void) {
    if (g_ws) {
        esp_websocket_client_stop(g_ws);
        esp_websocket_client_destroy(g_ws);
        g_ws = NULL;
    }
    g_connected = false;
    g_access_token[0] = '\0';
    g_recv_buf = NULL;
    g_recv_len = 0;
    g_recv_max = 0;
    g_recv_done = false;
    g_wait_task = NULL;
}