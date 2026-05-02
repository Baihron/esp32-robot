// voice_chat_client.c
#include "voice_chat_client.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "mbedtls/ssl.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/error.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "BAIDU_ASR";

#define BAIDU_TOKEN_HOST        "aip.baidubce.com"
#define BAIDU_TOKEN_PATH        "/oauth/2.0/token"

// 全局变量
static char g_access_token[256] = {0};
static bool g_initialized = false;
static char g_response_buf[4096] = {0};
static size_t g_response_len = 0;

// HTTP 事件处理回调（只保留一份）
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (evt->data_len > 0 && g_response_len + evt->data_len < sizeof(g_response_buf)) {
            memcpy(g_response_buf + g_response_len, evt->data, evt->data_len);
            g_response_len += evt->data_len;
            g_response_buf[g_response_len] = '\0';
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        g_response_buf[g_response_len] = '\0';
        ESP_LOGI(TAG, "Response: %s", g_response_buf);
        break;
    default:
        break;
    }
    return ESP_OK;
}

// ---------- 获取 access_token (保留，供扩展使用) ----------
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

    char request[2048];
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
    if (!response) {
        ESP_LOGI(TAG, "Response malloc failed");
        ret = -1;
        goto exit;
    }

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

    const char *body = strstr(response, "\r\n\r\n");
    if (body) {
        body += 4;
        const char *json_start = strchr(body, '{');
        if (json_start) {
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

// ---------- 初始化 ----------
esp_err_t voice_chat_client_init(void)
{
    if (g_initialized) return ESP_OK;

    if (baidu_get_access_token() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get access token");
        return ESP_FAIL;
    }

    g_initialized = true;
    ESP_LOGI(TAG, "✅ Voice ASR client initialized");
    return ESP_OK;
}

// ---------- 发送音频进行识别 (RAW 方式) ----------
esp_err_t voice_chat_client_send_audio(
    const int16_t *pcm_audio,
    size_t pcm_bytes,
    char *result_text,
    size_t result_size)
{
    if (!g_initialized) {
        ESP_LOGE(TAG, "Client not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (pcm_bytes == 0 || result_text == NULL || result_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    result_text[0] = '\0';

    // 构建完整 URL，包含 access_token 和 cuid、dev_pid
    char url[512];
    snprintf(url, sizeof(url), BAIDU_ASR_URL_FORMAT, g_access_token);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
        .buffer_size = 8192,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return ESP_FAIL;
    }

    // 设置 RAW 方式所需的 Content-Type
    esp_http_client_set_header(client, "Content-Type", "audio/pcm;rate=16000");

    // 直接上传 PCM 原始数据
    esp_http_client_set_post_field(client, (const char *)pcm_audio, pcm_bytes);

    // 重置响应缓冲区
    memset(g_response_buf, 0, sizeof(g_response_buf));
    g_response_len = 0;

    // 执行请求
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP status: %d", status_code);

    esp_http_client_cleanup(client); // 尽早释放客户端

    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP error response: %s", g_response_buf);
        return ESP_FAIL;
    }

    // 解析 JSON 响应
    cJSON *root = cJSON_Parse(g_response_buf);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON response: %s", g_response_buf);
        return ESP_FAIL;
    }

    cJSON *err_no = cJSON_GetObjectItem(root, "err_no");
    if (err_no && cJSON_IsNumber(err_no) && err_no->valueint == 0) {
        cJSON *result = cJSON_GetObjectItem(root, "result");
        if (result && cJSON_IsArray(result)) {
            cJSON *first = cJSON_GetArrayItem(result, 0);
            if (first && cJSON_IsString(first)) {
                strncpy(result_text, first->valuestring, result_size - 1);
                result_text[result_size - 1] = '\0';
                ESP_LOGI(TAG, "✅ Recognition result: %s", result_text);
            } else {
                ESP_LOGW(TAG, "Empty recognition result array");
                err = ESP_FAIL;
            }
        } else {
            ESP_LOGW(TAG, "No 'result' array in response");
            err = ESP_FAIL;
        }
    } else {
        cJSON *err_msg = cJSON_GetObjectItem(root, "err_msg");
        ESP_LOGE(TAG, "ASR error: err_no=%d, msg: %s",
                 err_no ? err_no->valueint : -1,
                 err_msg && cJSON_IsString(err_msg) ? err_msg->valuestring : "unknown");
        err = ESP_FAIL;
    }

    cJSON_Delete(root);
    return err;
}

void voice_chat_client_deinit(void) {
    g_initialized = false;
    g_access_token[0] = '\0';
    ESP_LOGI(TAG, "Voice ASR client deinitialized");
}