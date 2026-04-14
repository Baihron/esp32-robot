// my_board.cc
#include "my_board.h"
#include "esp_log.h"

static const char* TAG = "MyBoard";

MyBoard::MyBoard() : audio_codec_(nullptr) {
    ESP_LOGI(TAG, "MyBoard constructor");
    Board::SetInstance(this);
}

MyBoard::~MyBoard() {
    if (audio_codec_) {
        delete audio_codec_;
        audio_codec_ = nullptr;
    }
}

AudioCodec* MyBoard::GetAudioCodec() {
    if (audio_codec_ == nullptr) {
        audio_codec_ = new MyAudioCodec();
        ESP_LOGI(TAG, "Created MyAudioCodec instance");
    }
    return audio_codec_;
}

const char* MyBoard::GetBoardName() const {
    return "ESP32-S3-DevKitC-1";
}

// 注册这个板子
DECLARE_BOARD(MyBoard, "my_esp32s3_devkitc", "ESP32-S3 DevKitC-1")