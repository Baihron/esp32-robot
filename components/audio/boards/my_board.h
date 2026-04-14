// my_board.h
#pragma once

#include "board.h"
#include "my_audio_codec.h"

class MyBoard : public Board {
public:
    MyBoard();
    virtual ~MyBoard();

    virtual AudioCodec* GetAudioCodec() override;
    virtual const char* GetBoardName() const override;

private:
    MyAudioCodec* audio_codec_;
};