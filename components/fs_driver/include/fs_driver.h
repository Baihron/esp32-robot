#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// 文件信息结构体
typedef struct {
    char name[256];     // 文件名
    bool is_directory;  // 是否是目录
    size_t size;        // 文件大小（字节）
    time_t modified;    // 修改时间
} fs_file_info_t;

// SD卡信息结构体
typedef struct {
    uint64_t total_bytes;   // 总字节数
    uint64_t free_bytes;    // 可用字节数
    uint64_t used_bytes;    // 已用字节数
    char name[32];          // 卡名称
    uint32_t sector_size;   // 扇区大小
    uint32_t num_sectors;   // 扇区数量
} sd_card_info_t;

// SD卡配置结构体
typedef struct {
    int clk_pin;       // SD卡时钟引脚
    int cmd_pin;       // SD卡命令引脚
    int d0_pin;        // SD卡数据0引脚
    int d1_pin;        // SD卡数据1引脚（可选，-1表示不使用）
    int d2_pin;        // SD卡数据2引脚（可选，-1表示不使用）
    int d3_pin;        // SD卡数据3引脚（可选，-1表示不使用）
    int cd_pin;        // 卡检测引脚（可选，-1表示不使用）
    int width;         // 总线宽度（1或4）
    uint32_t freq_hz;  // 时钟频率
} sd_card_config_t;

/**
 * @brief 使用SDMMC模式初始化SD卡（使用默认配置）
 * @param mount_point 挂载点路径，如 "/sdcard"
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t fs_sd_card_init(const char* mount_point);

/**
 * @brief 使用SDMMC模式初始化SD卡
 * @param mount_point 挂载点路径，如 "/sdcard"
 * @param config SD卡配置（如果为NULL则使用默认配置）
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t fs_sd_card_init_sdmmc(const char* mount_point, const sd_card_config_t* config);

/**
 * @brief 获取SD卡详细信息
 * @param mount_point 挂载点路径
 * @param info SD卡信息结构体指针
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t fs_get_sd_card_info(const char* mount_point, sd_card_info_t* info);

/**
 * @brief 读取文件内容
 * @param file_path 文件完整路径
 * @param buffer 缓冲区指针
 * @param buffer_size 缓冲区大小
 * @param bytes_read 实际读取的字节数（输出）
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t fs_read_file(const char* file_path, uint8_t* buffer, size_t buffer_size, size_t* bytes_read);

/**
 * @brief 写入文件
 * @param file_path 文件完整路径
 * @param data 数据指针
 * @param data_size 数据大小
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t fs_write_file(const char* file_path, const uint8_t* data, size_t data_size);

/**
 * @brief 列出目录内容
 * @param dir_path 目录路径
 * @param file_list 文件信息数组
 * @param max_files 最大文件数
 * @param file_count 实际文件数（输出）
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t fs_list_directory(const char* dir_path, fs_file_info_t* file_list, int max_files, int* file_count);

/**
 * @brief 创建目录
 * @param dir_path 目录路径
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t fs_create_directory(const char* dir_path);

/**
 * @brief 删除文件或目录
 * @param path 路径
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t fs_delete(const char* path);

/**
 * @brief 检查文件或目录是否存在
 * @param path 路径
 * @return true 存在，false 不存在
 */
bool fs_exists(const char* path);

/**
 * @brief 获取文件大小
 * @param file_path 文件路径
 * @param size 文件大小（输出）
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t fs_get_file_size(const char* file_path, size_t* size);

/**
 * @brief 重命名文件或目录
 * @param old_path 原路径
 * @param new_path 新路径
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t fs_rename(const char* old_path, const char* new_path);

/**
 * @brief 复制文件
 * @param src_path 源文件路径
 * @param dst_path 目标文件路径
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t fs_copy_file(const char* src_path, const char* dst_path);

/**
 * @brief 获取文件系统信息
 * @param mount_point 挂载点路径
 * @param total_bytes 总字节数（输出）
 * @param used_bytes 已使用字节数（输出）
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t fs_get_info(const char* mount_point, size_t* total_bytes, size_t* used_bytes);

/**
 * @brief 卸载SD卡
 * @param mount_point 挂载点路径
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t fs_unmount(const char* mount_point);

/**
 * @brief 检查文件系统是否已挂载
 * @param mount_point 挂载点路径
 * @return true 已挂载，false 未挂载
 */
bool fs_is_mounted(const char* mount_point);

#ifdef __cplusplus
}
#endif