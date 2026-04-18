// components/audio/audio_manager.cpp
#include "audio_manager.h"
#include "audio_service.h"
#include "board.h"
#include "esp_log.h"
#include "my_board.h"

static const char *TAG = "AUDIO_MGR";

static AudioService* s_audio_service = nullptr;
static MyBoard my_board;
static bool s_is_running = false;

// 如果需要处理回调，可以定义静态函数
static void on_send_queue_available(void) {
    // 通知外部协议线程有数据可发，例如设置一个标志或发送信号量
    // 具体实现取决于你的协议任务设计
    ESP_LOGD(TAG, "Send queue available");
}

static void on_wake_word_detected(const std::string& wake_word) {
    ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
    // 可以在这里触发显示或事件
}

static void on_vad_change(bool speaking) {
    ESP_LOGD(TAG, "VAD changed: %s", speaking ? "speaking" : "silence");
}

esp_err_t audio_manager_init(void)
{
    if (s_audio_service != nullptr) {
        ESP_LOGW(TAG, "Audio manager already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing audio manager...");
    
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    if (!codec) {
        ESP_LOGE(TAG, "Failed to get audio codec from board");
        return ESP_ERR_INVALID_STATE;
    }

    // 2. 创建 AudioService 实例
    s_audio_service = new AudioService();
    if (!s_audio_service) {
        ESP_LOGE(TAG, "Failed to create AudioService");
        return ESP_ERR_NO_MEM;
    }

    // 3. 初始化 AudioService，传入 codec
    s_audio_service->Initialize(codec);

    // 4. 设置模型列表（如果需要唤醒词功能）
    //    s_audio_service->SetModelsList(models_list);  // models_list 需要从外部传入或从配置获取

    // // 5. 设置回调（可选）
    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available = on_send_queue_available;
    callbacks.on_wake_word_detected = on_wake_word_detected;
    callbacks.on_vad_change = on_vad_change;
    s_audio_service->SetCallbacks(callbacks);

    ESP_LOGI(TAG, "Audio manager initialized (not started)");
    return ESP_OK;
}

esp_err_t audio_manager_start(void)
{
    if (!s_audio_service) {
        ESP_LOGE(TAG, "Audio manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_is_running) {
        ESP_LOGW(TAG, "Audio service already running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting audio service...");
    s_audio_service->Start();
    s_is_running = true;
    return ESP_OK;
}

esp_err_t audio_manager_stop(void)
{
    if (!s_audio_service || !s_is_running) {
        ESP_LOGW(TAG, "Audio service not running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping audio service...");
    s_audio_service->Stop();
    s_is_running = false;
    return ESP_OK;
}

bool audio_manager_is_running(void)
{
    return s_is_running;
}

void* audio_manager_pop_send_packet(void)
{
    if (!s_audio_service || !s_is_running) {
        return nullptr;
    }
    // 返回 unique_ptr 的原始指针，调用者负责释放
    auto packet = s_audio_service->PopPacketFromSendQueue();
    if (!packet) {
        return nullptr;
    }
    // 注意：这里需要调用者知道如何释放这个指针，可以使用 delete
    return packet.release();
}

bool audio_manager_push_decode_packet(const uint8_t* data, size_t size, int sample_rate, int frame_duration, uint32_t timestamp)
{
    if (!s_audio_service || !s_is_running) {
        return false;
    }
    auto packet = std::make_unique<AudioStreamPacket>();
    packet->sample_rate = sample_rate;
    packet->frame_duration = frame_duration;
    packet->timestamp = timestamp;
    packet->payload.assign(data, data + size);
    return s_audio_service->PushPacketToDecodeQueue(std::move(packet), false);
}