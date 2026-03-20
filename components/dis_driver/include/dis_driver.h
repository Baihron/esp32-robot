#ifndef DIS_DRIVER_H
#define DIS_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "dis_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LCD显示配置结构体
 */
typedef struct {
    int sclk_gpio;      ///< SPI时钟引脚
    int mosi_gpio;      ///< SPI数据输出引脚
    int dc_gpio;        ///< 数据/命令选择引脚
    int cs_gpio;        ///< 片选引脚
    int rst_gpio;       ///< 复位引脚
    int en_gpio;        ///< 使能引脚（可选）
    uint16_t width;     ///< 屏幕宽度（像素）
    uint16_t height;    ///< 屏幕高度（像素）
    uint32_t pclk_hz;   ///< SPI时钟频率（Hz）
} dis_config_t;

/**
 * @brief 初始化LCD显示驱动
 * 
 * @param config 显示配置参数
 * @return esp_err_t 错误码
 */
esp_err_t dis_driver_init(const dis_config_t *config);

/**
 * @brief 反初始化LCD显示驱动
 */
void dis_driver_deinit(void);

/**
 * @brief 刷新帧缓冲区到LCD屏幕
 * 
 * @return esp_err_t 错误码
 */
esp_err_t dis_flush(void);

/**
 * @brief 获取帧缓冲区指针
 * 
 * @return uint16_t* 帧缓冲区指针
 */
uint16_t* dis_get_framebuffer(void);

/**
 * @brief 获取屏幕尺寸
 * 
 * @param width 宽度输出参数
 * @param height 高度输出参数
 */
void dis_get_size(uint16_t *width, uint16_t *height);

/**
 * @brief 诊断LCD驱动状态
 * 
 * 打印驱动内部状态信息，用于调试
 * 
 * @return esp_err_t 错误码
 */

#ifdef __cplusplus
}
#endif

#endif // DIS_DRIVER_H