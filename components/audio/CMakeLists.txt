idf_component_register(
    SRCS
        settings.cc
        audio_service.cc
        audio_manager.cpp
        audio_codec.cc
        boards/board.cc
        boards/my_board.cc
        codecs/my_audio_codec.cc
        codecs/no_audio_codec.cc
        demuxer/ogg_demuxer.cc
        processors/afe_audio_processor.cc
        processors/audio_debugger.cc
        processors/no_audio_processor.cc
        wake_words/afe_wake_word.cc   # 如果保留唤醒词

    INCLUDE_DIRS
        .
        boards
        codecs
        demuxer
        processors
        wake_words

    REQUIRES
        mico_driver
        driver
        nvs_flash
        esp_timer
        protocols

    PRIV_REQUIRES
        espressif__esp-sr
        espressif__esp_codec_dev
        espressif__esp_audio_codec
        espressif__esp_audio_effects
)

target_compile_options(${COMPONENT_LIB} PRIVATE 
    -Wall 
    -Wno-format
)
