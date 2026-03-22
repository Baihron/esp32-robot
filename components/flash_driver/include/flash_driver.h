#ifndef __FLASH_DRIVER_H__
#define __FLASH_DRIVER_H__

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 Flash 图片存储驱动
 *        必须在其他函数调用前调用
 * @return ESP_OK 成功，其他为失败
 */
esp_err_t face_flash_storage_init(void);

/**
 * @brief 保存图片到 Flash
 * @param data      图片数据指针
 * @param size      图片数据大小（字节）
 * @param slot      存储槽位（0 ~ FACE_FLASH_MAX_SLOTS-1）
 * @return ESP_OK 成功
 */
esp_err_t face_flash_storage_save(const uint8_t *data, size_t size, int slot);

/**
 * @brief 从 Flash 读取图片
 * @param slot      存储槽位
 * @param buf       读取缓冲区（调用者分配）
 * @param buf_size  缓冲区大小
 * @param out_size  实际读取的图片大小（输出）
 * @return ESP_OK 成功
 */
esp_err_t face_flash_storage_load(int slot, uint8_t *buf, size_t buf_size, size_t *out_size);

/**
 * @brief 擦除指定槽位的图片
 * @param slot 存储槽位
 * @return ESP_OK 成功
 */
esp_err_t face_flash_storage_erase(int slot);

/**
 * @brief 擦除整个图片分区
 * @return ESP_OK 成功
 */
esp_err_t face_flash_storage_erase_all(void);

/**
 * @brief 获取最大支持的槽位数
 */
int face_flash_storage_max_slots(void);

#ifdef __cplusplus
}
#endif

#endif