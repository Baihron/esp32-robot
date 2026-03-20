#pragma once

// LCD 引脚配置 - 请根据实际接线修改
#define DISPLAY_SCLK_GPIO    7
#define DISPLAY_MOSI_GPIO    6
#define DISPLAY_DC_GPIO      5
#define DISPLAY_CS_GPIO      4
#define DISPLAY_RST_GPIO     -1  // 不使用复位引脚
#define DISPLAY_EN_GPIO      -1  // 背光使能引脚，如果不用背光控制可以设为-1

// 屏幕尺寸
#define DISPLAY_WIDTH        240
#define DISPLAY_HEIGHT       240

// 时钟频率
#define DISPLAY_PCLK_HZ      (40 * 1000 * 1000)  // 40MHz