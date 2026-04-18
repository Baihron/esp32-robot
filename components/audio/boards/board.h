// board.h
#pragma once

#include "audio_codec.h"

class Board {
public:
    Board();
    virtual ~Board();
    
    virtual AudioCodec* GetAudioCodec() = 0;
    virtual const char* GetBoardName() const { return "Unknown Board"; }
    
    static Board& GetInstance() {
        return *instance_;
    }
    
    static void SetInstance(Board* board) {
        instance_ = board;
    }

protected:
    static Board* instance_;
};

#define DECLARE_BOARD(BoardClass, board_type, board_name) \
    extern "C" void register_board() { \
        Board::SetInstance(new BoardClass()); \
    }
