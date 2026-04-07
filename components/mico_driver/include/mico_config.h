#pragma once

// 麦克风引脚配置（根据上一步分析）
#define MIC_PIN_SCK     41   // BCLK / SCK
#define MIC_PIN_WS      42   // WS / LRCLK（PDM模式下作为CLK）
#define MIC_PIN_SD      40   // 数据输入 DIN

#define MIC_SAMPLE_RATE 16000
#define MIC_DMA_BUF_LEN 256
#define MIC_DMA_BUF_COUNT 8