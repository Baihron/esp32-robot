// components/audio/codecs/my_audio_codec.h
#pragma once
#include "audio_codec.h"

class MyAudioCodec : public AudioCodec {
public:
    MyAudioCodec();
    virtual ~MyAudioCodec();

    // 重写基类的虚函数
    virtual void Start() override;
    virtual void SetOutputVolume(int volume) override;
    virtual void SetInputGain(float gain) override;
    virtual void EnableInput(bool enable) override;
    virtual void EnableOutput(bool enable) override;

protected:
    // 实现 protected 纯虚函数
    virtual int Read(int16_t* dest, int samples) override;
    virtual int Write(const int16_t* data, int samples) override;

private:
    bool initialized_ = false;
};