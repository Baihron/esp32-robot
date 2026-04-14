// mico_config.h
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief INMP441 麦克风硬件引脚配置（I2S 标准模式）
 */
#define MIC_I2S_PORT          I2S_NUM_0       // 使用 I2S0
#define MIC_PIN_BCK           41              // I2S 位时钟 (BCK / SCK)
#define MIC_PIN_WS            42              // I2S 字选择 (WS / LRCLK)
#define MIC_PIN_DIN           40              // I2S 数据输入 (SD / DOUT)

/**
 * @brief 音频采样参数
 */
#define MIC_SAMPLE_RATE       16000           // 16kHz 采样率
#define MIC_BITS_PER_SAMPLE   16              // 最终输出 16-bit PCM
#define MIC_CHANNEL           1               // 单声道

/**
 * @brief DMA 缓冲区配置（影响实时性和内存占用）
 * @note   INMP441 输出 24-bit 数据在 32-bit 槽中，DMA 读取 32-bit 帧
 */
#define MIC_DMA_BUF_COUNT      8              // DMA 缓冲区数量
#define MIC_DMA_BUF_LEN        256            // 每个缓冲区的帧数（32-bit 帧）

#ifdef __cplusplus
}
#endif