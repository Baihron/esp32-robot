#include "voice_chat_client.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include "mbedtls/ssl.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/error.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "BAIDU_LLM";

#define BAIDU_TOKEN_HOST        "aip.baidubce.com"
#define BAIDU_TOKEN_PATH        "/oauth/2.0/token"
#define BAIDU_WS_URL            "wss://aip.baidubce.com/ws/2.0/speech/v1/realtime"

// 全局变量
static esp_websocket_client_handle_t g_ws = NULL;
static bool g_connected = false;
static char g_access_token[256] = {0};
static int16_t *g_recv_buf = NULL;
static size_t g_recv_len = 0;
static size_t g_recv_max = 0;
static bool g_recv_done = false;
static TaskHandle_t g_wait_task = NULL;

static esp_err_t ws_send_text(const char *json)
{
    if (!g_connected) return ESP_ERR_INVALID_STATE;
    int ret = esp_websocket_client_send_text(g_ws, json, strlen(json), pdMS_TO_TICKS(3000));
    return ret < 0 ? ESP_FAIL : ESP_OK;
}

// ---------- 获取 access_token ----------
static esp_err_t baidu_get_access_token(void)
{
    if (g_access_token[0] != '\0') return ESP_OK;

    char post_body[512];
    snprintf(post_body, sizeof(post_body),
             "grant_type=client_credentials&client_id=%s&client_secret=%s",
             BAIDU_API_KEY, BAIDU_SECRET_KEY);

    mbedtls_net_context server_fd;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context entropy;

    mbedtls_net_init(&server_fd);
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_entropy_init(&entropy);

    int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0);
    if (ret) { ESP_LOGE(TAG, "drbg_seed err -0x%x", -ret); goto exit; }

    ret = mbedtls_net_connect(&server_fd, BAIDU_TOKEN_HOST, "443", MBEDTLS_NET_PROTO_TCP);
    if (ret) { ESP_LOGE(TAG, "connect err -0x%x", -ret); goto exit; }

    ret = mbedtls_ssl_config_defaults(&conf,
                                      MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret) { ESP_LOGE(TAG, "config_defaults err -0x%x", -ret); goto exit; }

    esp_crt_bundle_attach(&conf);

    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
    ret = mbedtls_ssl_setup(&ssl, &conf);
    if (ret) { ESP_LOGE(TAG, "ssl_setup err -0x%x", -ret); goto exit; }
    ret = mbedtls_ssl_set_hostname(&ssl, BAIDU_TOKEN_HOST);
    if (ret) { ESP_LOGE(TAG, "set_hostname err -0x%x", -ret); goto exit; }
    mbedtls_ssl_set_bio(&ssl, &server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);

    while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            ESP_LOGE(TAG, "handshake err -0x%x", -ret);
            goto exit;
        }
    }

    char request[1024];
    snprintf(request, sizeof(request),
             "POST %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Content-Type: application/x-www-form-urlencoded\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n"
             "\r\n"
             "%s",
             BAIDU_TOKEN_PATH, BAIDU_TOKEN_HOST, (int)strlen(post_body), post_body);

    while ((ret = mbedtls_ssl_write(&ssl, (const unsigned char *)request, strlen(request))) <= 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            ESP_LOGE(TAG, "write err -0x%x", -ret);
            goto exit;
        }
    }

    size_t cap = 1024;
    char *response = malloc(cap);
    if (!response) { ret = -1; goto exit; }
    int total = 0;
    while (1) {
        if (total + 1 >= cap) {
            cap *= 2;
            char *tmp = realloc(response, cap);
            if (!tmp) { free(response); ret = -1; goto exit; }
            response = tmp;
        }
        ret = mbedtls_ssl_read(&ssl, (unsigned char *)(response + total), cap - total - 1);
        if (ret <= 0) break;
        total += ret;
    }
    response[total] = '\0';
    ESP_LOGI(TAG, "Response total %d bytes", total);

    const char *body = strstr(response, "\r\n\r\n");
    if (body) {
        body += 4;
        const char *json_start = strchr(body, '{');
        if (json_start) {
            ESP_LOGI(TAG, "Token JSON: %.200s...", json_start);
            cJSON *root = cJSON_Parse(json_start);
            if (root) {
                cJSON *tok = cJSON_GetObjectItem(root, "access_token");
                if (tok && cJSON_IsString(tok)) {
                    strncpy(g_access_token, tok->valuestring, sizeof(g_access_token) - 1);
                    g_access_token[sizeof(g_access_token) - 1] = '\0';
                    ESP_LOGI(TAG, "✅ access_token: %.10s...", g_access_token);
                } else {
                    ESP_LOGE(TAG, "access_token not found in JSON");
                }
                cJSON_Delete(root);
            } else {
                ESP_LOGE(TAG, "Invalid JSON");
            }
        } else {
            ESP_LOGE(TAG, "No JSON body in response");
        }
    } else {
        ESP_LOGE(TAG, "No HTTP body in response");
    }
    free(response);

exit:
    mbedtls_ssl_close_notify(&ssl);
    mbedtls_net_free(&server_fd);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    return (g_access_token[0] != '\0') ? ESP_OK : ESP_FAIL;
}

// ---------- WebSocket 事件处理（已修复 use-after-free） ----------
static void ws_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_websocket_event_data_t *ed = (esp_websocket_event_data_t *)data;

    if (id == WEBSOCKET_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "✅ WebSocket connected");
        g_connected = true;
    } else if (id == WEBSOCKET_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "❌ WebSocket disconnected");
        g_connected = false;
        TaskHandle_t task = g_wait_task;
        g_wait_task = NULL;
        if (task) xTaskNotifyGive(task);
    } else if (id == WEBSOCKET_EVENT_DATA && ed->op_code == 0x1) {
        char json[2048];
        size_t len = ed->data_len < sizeof(json) - 1 ? ed->data_len : sizeof(json) - 1;
        memcpy(json, ed->data_ptr, len);
        json[len] = '\0';

        cJSON *root = cJSON_Parse(json);
        if (!root) return;

        cJSON *type = cJSON_GetObjectItem(root, "type");
        if (type && cJSON_IsString(type)) {
            const char *event = type->valuestring;
            ESP_LOGI(TAG, "WS event: %s", event);

            if (strcmp(event, "response.audio.delta") == 0) {
                // 如果已经没有接收缓冲区，直接丢弃
                if (g_recv_buf == NULL) {
                    ESP_LOGW(TAG, "Audio delta ignored: no recv buffer");
                    cJSON_Delete(root);
                    return;
                }
                cJSON *delta = cJSON_GetObjectItem(root, "delta");
                if (delta && cJSON_IsString(delta)) {
                    const char *b64 = delta->valuestring;
                    size_t b64_len = strlen(b64);
                    size_t max_dec = (b64_len * 3) / 4 + 4;
                    if (g_recv_len + max_dec <= g_recv_max) {
                        size_t olen = 0;
                        int ret = mbedtls_base64_decode(
                            (unsigned char *)g_recv_buf + g_recv_len,
                            g_recv_max - g_recv_len, &olen,
                            (const unsigned char *)b64, b64_len);
                        if (ret == 0) {
                            g_recv_len += olen;
                            ESP_LOGI(TAG, "Audio delta: %d bytes (total %d)", olen, g_recv_len);
                        } else {
                            ESP_LOGE(TAG, "base64 decode error: %d", ret);
                        }
                    } else {
                        ESP_LOGE(TAG, "Audio buffer overflow! need %d, have %d", max_dec, g_recv_max - g_recv_len);
                    }
                }
            } else if (strcmp(event, "response.done") == 0) {
                TaskHandle_t task = g_wait_task;
                g_wait_task = NULL;
                g_recv_buf = NULL;   // 防止后续写入
                if (task) {
                    xTaskNotifyGive(task);
                }
            } else if (strcmp(event, "error") == 0) {
                TaskHandle_t task = g_wait_task;
                g_wait_task = NULL;
                g_recv_buf = NULL;
                if (task) {
                    xTaskNotifyGive(task);
                }
                // 打印完整的错误 JSON
                ESP_LOGE(TAG, "❌ Server error event received");
                cJSON *error_copy = cJSON_Duplicate(root, 1);
                if (error_copy) {
                    char *error_str = cJSON_Print(error_copy);
                    ESP_LOGE(TAG, "Full error JSON:\n%s", error_str);
                    free(error_str);
                    cJSON_Delete(error_copy);
                } else {
                    ESP_LOGE(TAG, "Error raw JSON: %.*s", (int)len, json);
                }
            }
        }
        cJSON_Delete(root);
    }
}

// ---------- 初始化 ----------
esp_err_t voice_chat_client_init(void)
{
    if (g_ws) return ESP_OK;

    if (baidu_get_access_token() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get access token");
        return ESP_FAIL;
    }

    char ws_url[512];
    snprintf(ws_url, sizeof(ws_url), "%s?model=%s&access_token=%s",
             BAIDU_WS_URL, BAIDU_MODEL, g_access_token);

    esp_websocket_client_config_t cfg = {
        .uri = ws_url,
        .task_prio = tskIDLE_PRIORITY + 5,
        .task_stack = 16384,
        .buffer_size = 8192,
        .network_timeout_ms = 5000,
        .reconnect_timeout_ms = 0,
        .skip_cert_common_name_check = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    g_ws = esp_websocket_client_init(&cfg);
    if (!g_ws) return ESP_FAIL;

    esp_websocket_register_events(g_ws, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL);
    esp_websocket_client_start(g_ws);

    int timeout = 0;
    while (!g_connected && timeout < 50) {
        vTaskDelay(pdMS_TO_TICKS(100));
        timeout++;
    }
    if (!g_connected) {
        voice_chat_client_deinit();
        return ESP_ERR_TIMEOUT;
    }

    // 发送 session.update
    esp_websocket_client_send_text(g_ws,
        "{\"type\":\"session.update\",\"session\":{\"turn_detection\":{\"type\":\"server_vad\",\"create_response\":true,\"interrupt_response\":true}}}",
        strlen("{\"type\":\"session.update\",\"session\":{\"turn_detection\":{\"type\":\"server_vad\",\"create_response\":true,\"interrupt_response\":true}}}"),
        pdMS_TO_TICKS(3000));

    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "✅ Voice chat client initialized");
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

    g_recv_buf = reply_buf;
    g_recv_len = 0;
    g_recv_max = max_reply_len;
    g_recv_done = false;
    g_wait_task = xTaskGetCurrentTaskHandle();

    size_t offset = 0;
    while (offset < pcm_bytes) {
        size_t chunk_size = (pcm_bytes - offset) > AUDIO_FRAME_BYTES ? AUDIO_FRAME_BYTES : (pcm_bytes - offset);
        
        size_t b64_needed = 4 * ((chunk_size + 2) / 3) + 1;
        char *b64_buf = malloc(b64_needed);
        if (!b64_buf) return ESP_ERR_NO_MEM;

        size_t olen = 0;
        int ret = mbedtls_base64_encode((unsigned char *)b64_buf, b64_needed, &olen,
                                        (const unsigned char *)pcm_audio + offset, chunk_size);
        if (ret != 0) {
            free(b64_buf);
            return ESP_FAIL;
        }
        b64_buf[olen] = '\0';

        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "type", "input_audio_buffer.append");
        cJSON_AddStringToObject(root, "audio", b64_buf);
        char *json_str = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        free(b64_buf);

        esp_err_t err = ws_send_text(json_str);
        free(json_str);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "❌ 发送音频帧失败");
            return err;
        }

        offset += chunk_size;
        vTaskDelay(pdMS_TO_TICKS(AUDIO_FRAME_MS));
    }

    // 等待 response.done 或 error 事件（超时 15 秒）
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(15000));

    // 无论是否超时，清空全局指针，避免悬空
    g_recv_buf = NULL;
    g_wait_task = NULL;

    *reply_len = g_recv_len;
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