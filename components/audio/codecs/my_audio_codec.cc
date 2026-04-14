// components/audio/codecs/my_audio_codec.cc
#include "my_audio_codec.h"
#include "mico_driver.h"
#include "esp_log.h"

static const char* TAG = "MyAudioCodec";

MyAudioCodec::MyAudioCodec() {
    // 设置音频参数（基类的成员变量）
    duplex_ = false;              // 只录音，不播放
    input_reference_ = false;
    input_sample_rate_ = 16000;
    output_sample_rate_ = 16000;
    input_channels_ = 1;
    output_channels_ = 1;
    input_enabled_ = true;
    output_enabled_ = false;
}

MyAudioCodec::~MyAudioCodec() {
    if (initialized_) {
        mico_driver_deinit();
        initialized_ = false;
    }
}

void MyAudioCodec::Start() {
    ESP_LOGI(TAG, "Starting MyAudioCodec");
    if (!initialized_) {
        esp_err_t ret = mico_driver_init();
        if (ret == ESP_OK) {
            initialized_ = true;
            ESP_LOGI(TAG, "Microphone initialized successfully");
        } else {
            ESP_LOGE(TAG, "Failed to init microphone: %s", esp_err_to_name(ret));
        }
    }
}

void MyAudioCodec::SetOutputVolume(int volume) {
    output_volume_ = volume;
    ESP_LOGI(TAG, "Set output volume to %d", volume);
}

void MyAudioCodec::SetInputGain(float gain) {
    input_gain_ = gain;
    ESP_LOGI(TAG, "Set input gain to %.2f", gain);
}

void MyAudioCodec::EnableInput(bool enable) {
    input_enabled_ = enable;
    ESP_LOGI(TAG, "Input %s", enable ? "enabled" : "disabled");
}

void MyAudioCodec::EnableOutput(bool enable) {
    output_enabled_ = enable;
    ESP_LOGI(TAG, "Output %s", enable ? "enabled" : "disabled");
}

int MyAudioCodec::Read(int16_t* dest, int samples) {
    if (!initialized_ || !input_enabled_) {
        return 0;
    }
    
    size_t bytes_read = 0;
    esp_err_t ret = mico_driver_read(dest, samples * sizeof(int16_t), &bytes_read);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read failed: %s", esp_err_to_name(ret));
        return 0;
    }
    
    int samples_read = bytes_read / sizeof(int16_t);
    return samples_read;
}

int MyAudioCodec::Write(const int16_t* data, int samples) {
    // 如果没有扬声器，直接返回写入的样本数（假装成功）
    return samples;
}