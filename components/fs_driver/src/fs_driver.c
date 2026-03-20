#include "fs_driver.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <ff.h>
// #include <sys/statvfs.h>

static const char *TAG = "FS_DRIVER";

// SD卡默认引脚配置（根据你的接线修改）
#define DEFAULT_SD_CLK_PIN     GPIO_NUM_39
#define DEFAULT_SD_CMD_PIN     GPIO_NUM_38
#define DEFAULT_SD_D0_PIN      GPIO_NUM_40
#define DEFAULT_SD_D1_PIN      -1
#define DEFAULT_SD_D2_PIN      -1
#define DEFAULT_SD_D3_PIN      -1
#define DEFAULT_SD_CD_PIN      -1
#define DEFAULT_SD_WIDTH       1  // 1-bit模式
#define DEFAULT_SD_FREQ_HZ     4000000  // 初始频率4MHz，之后可提高到20MHz

// 全局变量保存SD卡信息
static sdmmc_card_t* g_sd_card = NULL;
static char g_sd_mount_point[32] = "";

// 默认配置
static esp_vfs_fat_sdmmc_mount_config_t mount_config = {
    .format_if_mount_failed = false,
    .max_files = 5,
    .allocation_unit_size = 16 * 1024,
    .disk_status_check_enable = false
};

// 默认SD卡配置
static sd_card_config_t default_sd_config = {
    .clk_pin = DEFAULT_SD_CLK_PIN,
    .cmd_pin = DEFAULT_SD_CMD_PIN,
    .d0_pin = DEFAULT_SD_D0_PIN,
    .d1_pin = DEFAULT_SD_D1_PIN,
    .d2_pin = DEFAULT_SD_D2_PIN,
    .d3_pin = DEFAULT_SD_D3_PIN,
    .cd_pin = DEFAULT_SD_CD_PIN,
    .width = DEFAULT_SD_WIDTH,
    .freq_hz = DEFAULT_SD_FREQ_HZ
};

esp_err_t fs_sd_card_init_sdmmc(const char* mount_point, const sd_card_config_t* config)
{
    // 检查是否已经挂载到相同挂载点
    if (g_sd_card != NULL && strcmp(g_sd_mount_point, mount_point) == 0) {
        ESP_LOGW(TAG, "SD卡已经挂载到 %s，无需重复挂载", mount_point);
        return ESP_OK;
    }
    
    // 如果已经挂载到不同挂载点，先卸载
    if (g_sd_card != NULL) {
        ESP_LOGW(TAG, "SD卡已经挂载到 %s，正在卸载...", g_sd_mount_point);
        fs_unmount(g_sd_mount_point);
    }

    ESP_LOGI(TAG, "使用SDMMC模式初始化SD卡...");
    
    // 保存挂载点
    strncpy(g_sd_mount_point, mount_point, sizeof(g_sd_mount_point) - 1);
    
    // 使用传入的配置或默认配置
    sd_card_config_t sd_config;
    if (config != NULL) {
        memcpy(&sd_config, config, sizeof(sd_card_config_t));
    } else {
        memcpy(&sd_config, &default_sd_config, sizeof(sd_card_config_t));
    }
    
    ESP_LOGI(TAG, "SD卡配置: CLK=%d, CMD=%d, D0=%d, D1=%d, D2=%d, D3=%d, CD=%d, 宽度=%d-bit, 频率=%dHz",
             sd_config.clk_pin, sd_config.cmd_pin, sd_config.d0_pin,
             sd_config.d1_pin, sd_config.d2_pin, sd_config.d3_pin,
             sd_config.cd_pin, sd_config.width, sd_config.freq_hz);
    
    // SDMMC主机配置
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    
    // 设置时钟频率
    host.max_freq_khz = sd_config.freq_hz / 1000;
    
    // SDMMC总线配置
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk = sd_config.clk_pin;
    slot_config.cmd = sd_config.cmd_pin;
    slot_config.d0 = sd_config.d0_pin;
    slot_config.width = sd_config.width;
    slot_config.cd = sd_config.cd_pin;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    
    // 可选的数据引脚
    if (sd_config.d1_pin >= 0) slot_config.d1 = sd_config.d1_pin;
    if (sd_config.d2_pin >= 0) slot_config.d2 = sd_config.d2_pin;
    if (sd_config.d3_pin >= 0) slot_config.d3 = sd_config.d3_pin;
    
    // 挂载SD卡
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &g_sd_card);
    
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "挂载失败，请检查SD卡是否正确插入或格式化");
        } else {
            ESP_LOGE(TAG, "SD卡初始化失败: %s", esp_err_to_name(ret));
        }
        return ret;
    }
    
    // 打印SD卡信息
    sdmmc_card_print_info(stdout, g_sd_card);
    ESP_LOGI(TAG, "SD卡挂载成功到: %s", mount_point);
    
    return ESP_OK;
}

esp_err_t fs_get_sd_card_info(const char* mount_point, sd_card_info_t* info)
{
    if (!g_sd_card) {
        ESP_LOGE(TAG, "SD卡未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 填充SD卡信息
    memset(info, 0, sizeof(sd_card_info_t));
    
    // 方法1：使用FATFS API获取文件系统信息
    FATFS* fs;
    DWORD free_clusters, total_clusters;
    
    // 使用默认驱动器号 "0:"
    FRESULT res = f_getfree("0:", &free_clusters, &fs);
    if (res != FR_OK) {
        ESP_LOGE(TAG, "获取SD卡空间信息失败: %d", res);
        
        // 如果FATFS API失败，尝试使用SD卡原始信息
        ESP_LOGW(TAG, "使用SD卡原始信息");
        
        // 从SD卡CSD信息获取容量
        uint64_t card_capacity = (uint64_t)g_sd_card->csd.capacity * g_sd_card->csd.sector_size;
        
        // 假设使用率为50%（因为没有文件系统信息）
        info->total_bytes = card_capacity;
        info->used_bytes = card_capacity / 2;
        info->free_bytes = card_capacity / 2;
        
        // SD卡基本信息
        if (g_sd_card->cid.name[0] != 0) {
            strncpy(info->name, (char*)g_sd_card->cid.name, sizeof(info->name) - 1);
        } else {
            strncpy(info->name, "SD Card", sizeof(info->name) - 1);
        }
        info->sector_size = g_sd_card->csd.sector_size;
        info->num_sectors = g_sd_card->csd.capacity;
        
        ESP_LOGI(TAG, "SD卡信息 (原始信息，估算):");
        ESP_LOGI(TAG, "  名称: %s", info->name);
        ESP_LOGI(TAG, "  总大小: %.2f MB", info->total_bytes / (1024.0 * 1024.0));
        ESP_LOGI(TAG, "  扇区大小: %d 字节", info->sector_size);
        ESP_LOGI(TAG, "  扇区数量: %d", info->num_sectors);
        ESP_LOGW(TAG, "  注意：使用空间为估算值");
        
        return ESP_OK;
    }
    
    // FATFS API成功，计算精确的空间信息
    total_clusters = (fs->n_fatent - 2);
    uint32_t sector_size = g_sd_card->csd.sector_size;
    uint32_t sectors_per_cluster = fs->csize;
    
    info->total_bytes = (uint64_t)total_clusters * sectors_per_cluster * sector_size;
    info->free_bytes = (uint64_t)free_clusters * sectors_per_cluster * sector_size;
    info->used_bytes = info->total_bytes - info->free_bytes;
    
    // SD卡基本信息
    if (g_sd_card->cid.name[0] != 0) {
        strncpy(info->name, (char*)g_sd_card->cid.name, sizeof(info->name) - 1);
    } else {
        strncpy(info->name, "SD Card", sizeof(info->name) - 1);
    }
    info->sector_size = sector_size;
    info->num_sectors = g_sd_card->csd.capacity;
    
    ESP_LOGI(TAG, "SD卡信息:");
    ESP_LOGI(TAG, "  名称: %s", info->name);
    ESP_LOGI(TAG, "  总大小: %.2f MB", info->total_bytes / (1024.0 * 1024.0));
    ESP_LOGI(TAG, "  已用: %.2f MB", info->used_bytes / (1024.0 * 1024.0));
    ESP_LOGI(TAG, "  可用: %.2f MB", info->free_bytes / (1024.0 * 1024.0));
    ESP_LOGI(TAG, "  扇区大小: %d 字节", info->sector_size);
    ESP_LOGI(TAG, "  扇区数量: %d", info->num_sectors);
    
    return ESP_OK;
}

esp_err_t fs_read_file(const char* file_path, uint8_t* buffer, size_t buffer_size, size_t* bytes_read)
{
    if (file_path == NULL || buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    FILE* file = fopen(file_path, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "无法打开文件: %s, 错误: %s", file_path, strerror(errno));
        return ESP_FAIL;
    }
    
    // 获取文件大小
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size < 0) {
        fclose(file);
        ESP_LOGE(TAG, "获取文件大小失败: %s", file_path);
        return ESP_FAIL;
    }
    
    if ((size_t)file_size > buffer_size) {
        ESP_LOGW(TAG, "文件太大 (%ld字节)，缓冲区只有 %zu字节", file_size, buffer_size);
        file_size = buffer_size;
    }
    
    // 读取文件内容
    size_t read_size = fread(buffer, 1, file_size, file);
    fclose(file);
    
    if (read_size != (size_t)file_size) {
        ESP_LOGE(TAG, "读取文件失败: %s, 期望 %ld字节，实际 %zu字节", 
                file_path, file_size, read_size);
        return ESP_FAIL;
    }
    
    if (bytes_read) {
        *bytes_read = read_size;
    }
    
    ESP_LOGI(TAG, "成功读取文件: %s (%zu字节)", file_path, read_size);
    return ESP_OK;
}

esp_err_t fs_write_file(const char* file_path, const uint8_t* data, size_t data_size)
{
    if (file_path == NULL || data == NULL || data_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    FILE* file = fopen(file_path, "wb");
    if (file == NULL) {
        ESP_LOGE(TAG, "无法创建文件: %s, 错误: %s", file_path, strerror(errno));
        return ESP_FAIL;
    }
    
    size_t written = fwrite(data, 1, data_size, file);
    fclose(file);
    
    if (written != data_size) {
        ESP_LOGE(TAG, "写入文件失败: %s, 期望 %zu字节，实际 %zu字节", 
                file_path, data_size, written);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "成功写入文件: %s (%zu字节)", file_path, written);
    return ESP_OK;
}

esp_err_t fs_list_directory(const char* dir_path, fs_file_info_t* file_list, int max_files, int* file_count)
{
    if (dir_path == NULL || file_list == NULL || max_files <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    DIR* dir = opendir(dir_path);
    if (dir == NULL) {
        ESP_LOGE(TAG, "无法打开目录: %s, 错误: %s", dir_path, strerror(errno));
        return ESP_FAIL;
    }
    
    struct dirent* entry;
    struct stat file_stat;
    int count = 0;
    char full_path[512];
    
    while ((entry = readdir(dir)) != NULL && count < max_files) {
        // 跳过 "." 和 ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // 构建完整路径
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        
        // 获取文件信息
        if (stat(full_path, &file_stat) == 0) {
            fs_file_info_t* info = &file_list[count];
            
            strncpy(info->name, entry->d_name, sizeof(info->name) - 1);
            info->is_directory = S_ISDIR(file_stat.st_mode);
            info->size = file_stat.st_size;
            info->modified = file_stat.st_mtime;
            
            count++;
        }
    }
    
    closedir(dir);
    
    if (file_count) {
        *file_count = count;
    }
    
    ESP_LOGI(TAG, "列出目录 %s: 找到 %d 个项目", dir_path, count);
    return ESP_OK;
}

esp_err_t fs_create_directory(const char* dir_path)
{
    if (dir_path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查目录是否已存在
    struct stat st;
    if (stat(dir_path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            ESP_LOGW(TAG, "目录已存在: %s", dir_path);
            return ESP_OK;
        } else {
            ESP_LOGE(TAG, "路径已存在但不是目录: %s", dir_path);
            return ESP_FAIL;
        }
    }
    
    // 创建目录
    if (mkdir(dir_path, 0755) != 0) {
        ESP_LOGE(TAG, "创建目录失败: %s, 错误: %s", dir_path, strerror(errno));
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "创建目录成功: %s", dir_path);
    return ESP_OK;
}

esp_err_t fs_delete(const char* path)
{
    if (path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    struct stat st;
    if (stat(path, &st) != 0) {
        ESP_LOGE(TAG, "路径不存在: %s", path);
        return ESP_FAIL;
    }
    
    if (S_ISDIR(st.st_mode)) {
        // 删除目录（需要先删除目录内所有文件）
        DIR* dir = opendir(path);
        if (dir) {
            struct dirent* entry;
            char full_path[512];
            
            while ((entry = readdir(dir)) != NULL) {
                // 跳过 "." 和 ".."
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                    continue;
                }
                
                snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
                fs_delete(full_path); // 递归删除
            }
            closedir(dir);
        }
        
        // 删除空目录
        if (rmdir(path) != 0) {
            ESP_LOGE(TAG, "删除目录失败: %s, 错误: %s", path, strerror(errno));
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "删除目录成功: %s", path);
    } else {
        // 删除文件
        if (unlink(path) != 0) {
            ESP_LOGE(TAG, "删除文件失败: %s, 错误: %s", path, strerror(errno));
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "删除文件成功: %s", path);
    }
    
    return ESP_OK;
}

bool fs_exists(const char* path)
{
    if (path == NULL) {
        return false;
    }
    
    struct stat st;
    return (stat(path, &st) == 0);
}

esp_err_t fs_get_file_size(const char* file_path, size_t* size)
{
    if (file_path == NULL || size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    struct stat st;
    if (stat(file_path, &st) != 0) {
        ESP_LOGE(TAG, "文件不存在: %s", file_path);
        return ESP_FAIL;
    }
    
    if (!S_ISREG(st.st_mode)) {
        ESP_LOGE(TAG, "不是普通文件: %s", file_path);
        return ESP_FAIL;
    }
    
    *size = st.st_size;
    return ESP_OK;
}

esp_err_t fs_rename(const char* old_path, const char* new_path)
{
    if (old_path == NULL || new_path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (rename(old_path, new_path) != 0) {
        ESP_LOGE(TAG, "重命名失败: %s -> %s, 错误: %s", 
                old_path, new_path, strerror(errno));
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "重命名成功: %s -> %s", old_path, new_path);
    return ESP_OK;
}

esp_err_t fs_copy_file(const char* src_path, const char* dst_path)
{
    if (src_path == NULL || dst_path == NULL) {
        ESP_LOGE(TAG, "复制文件: 参数无效");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "复制文件: 源路径='%s', 目标路径='%s'", src_path, dst_path);
    
    // 检查源文件是否存在
    if (!fs_exists(src_path)) {
        ESP_LOGE(TAG, "复制文件: 源文件不存在: %s", src_path);
        return ESP_FAIL;
    }
    
    // 获取源文件大小
    size_t src_size;
    if (fs_get_file_size(src_path, &src_size) != ESP_OK) {
        ESP_LOGE(TAG, "复制文件: 无法获取源文件大小");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "源文件大小: %zu 字节", src_size);
    
    // 方法1：使用标准C库函数（更可靠）
    FILE* src_file = fopen(src_path, "rb");
    if (src_file == NULL) {
        ESP_LOGE(TAG, "复制文件: 无法打开源文件: %s, 错误: %s (errno=%d)", 
                src_path, strerror(errno), errno);
        return ESP_FAIL;
    }
    
    FILE* dst_file = fopen(dst_path, "wb");
    if (dst_file == NULL) {
        ESP_LOGE(TAG, "复制文件: 无法创建目标文件: %s, 错误: %s (errno=%d)", 
                dst_path, strerror(errno), errno);
        
        // 尝试诊断问题
        char* dir_path = strdup(dst_path);
        if (dir_path) {
            char* last_slash = strrchr(dir_path, '/');
            if (last_slash) {
                *last_slash = '\0';
                ESP_LOGI(TAG, "目标目录: %s", dir_path);
                
                // 检查目录是否存在
                struct stat st;
                if (stat(dir_path, &st) == 0) {
                    if (S_ISDIR(st.st_mode)) {
                        ESP_LOGI(TAG, "目录存在且有权限");
                    } else {
                        ESP_LOGE(TAG, "路径存在但不是目录");
                    }
                } else {
                    ESP_LOGE(TAG, "目录不存在: %s", dir_path);
                }
            }
            free(dir_path);
        }
        
        fclose(src_file);
        return ESP_FAIL;
    }
    
    // 使用较小的缓冲区，避免内存问题
    uint8_t buffer[512];  // 使用栈缓冲区，避免堆分配问题
    size_t total_read = 0;
    size_t bytes_read;
    esp_err_t ret = ESP_OK;
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), src_file)) > 0) {
        size_t bytes_written = fwrite(buffer, 1, bytes_read, dst_file);
        if (bytes_written != bytes_read) {
            ESP_LOGE(TAG, "复制文件: 写入失败，期望 %zu 字节，实际 %zu 字节", 
                    bytes_read, bytes_written);
            ret = ESP_FAIL;
            break;
        }
        total_read += bytes_read;
        
        // 每64KB打印一次进度
        if (total_read % (64 * 1024) < sizeof(buffer)) {
            ESP_LOGI(TAG, "复制进度: %zu / %zu 字节 (%.1f%%)", 
                    total_read, src_size, (float)total_read * 100 / src_size);
        }
    }
    
    // 检查错误
    if (ferror(src_file)) {
        ESP_LOGE(TAG, "复制文件: 读取源文件时发生错误");
        ret = ESP_FAIL;
    }
    
    if (ferror(dst_file)) {
        ESP_LOGE(TAG, "复制文件: 写入目标文件时发生错误");
        ret = ESP_FAIL;
    }
    
    // 关闭文件
    fclose(src_file);
    fclose(dst_file);
    
    if (ret != ESP_OK) {
        // 删除部分写入的文件
        unlink(dst_path);
        ESP_LOGE(TAG, "复制文件失败: %s -> %s", src_path, dst_path);
        return ret;
    }
    
    // 验证复制结果
    size_t dst_size;
    if (fs_get_file_size(dst_path, &dst_size) == ESP_OK) {
        if (dst_size == src_size) {
            ESP_LOGI(TAG, "复制文件成功: %s -> %s (%zu字节)", src_path, dst_path, total_read);
            return ESP_OK;
        } else {
            ESP_LOGE(TAG, "复制文件大小不匹配: 源=%zu, 目标=%zu", src_size, dst_size);
            unlink(dst_path);
            return ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "无法验证复制结果");
        unlink(dst_path);
        return ESP_FAIL;
    }
}

esp_err_t fs_get_info(const char* mount_point, size_t* total_bytes, size_t* used_bytes)
{
    if (mount_point == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查是否是SD卡挂载点
    if (strcmp(mount_point, g_sd_mount_point) == 0 && g_sd_card != NULL) {
        sd_card_info_t info;
        esp_err_t ret = fs_get_sd_card_info(mount_point, &info);
        if (ret == ESP_OK) {
            if (total_bytes) *total_bytes = info.total_bytes;
            if (used_bytes) *used_bytes = info.used_bytes;
            return ESP_OK;
        }
    }
    
    // 简化处理：对于非SD卡文件系统，返回不支持
    ESP_LOGW(TAG, "无法获取文件系统信息: %s (仅支持SD卡)", mount_point);
    
    if (total_bytes) *total_bytes = 0;
    if (used_bytes) *used_bytes = 0;
    
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t fs_unmount(const char* mount_point)
{
    ESP_LOGI(TAG, "卸载文件系统: %s", mount_point);
    
    // 检查是否是SD卡
    if (strcmp(mount_point, g_sd_mount_point) == 0 && g_sd_card != NULL) {
        // 卸载SD卡
        esp_err_t ret = esp_vfs_fat_sdcard_unmount(mount_point, g_sd_card);
        if (ret == ESP_OK) {
            g_sd_card = NULL;
            ESP_LOGI(TAG, "SD卡卸载成功");
            return ESP_OK;
        } else {
            ESP_LOGE(TAG, "SD卡卸载失败: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    
    // 对于其他文件系统，目前只支持SD卡卸载
    ESP_LOGW(TAG, "卸载失败: 未找到挂载点 %s 或不是SD卡", mount_point);
    return ESP_ERR_NOT_FOUND;
}

bool fs_is_mounted(const char* mount_point)
{
    if (mount_point == NULL) {
        return false;
    }
    
    // 检查SD卡
    if (strcmp(mount_point, g_sd_mount_point) == 0 && g_sd_card != NULL) {
        return true;
    }
    
    // 检查其他文件系统
    DIR* dir = opendir(mount_point);
    if (dir != NULL) {
        closedir(dir);
        return true;
    }
    return false;
}

// 为了向后兼容，保留原有函数名
esp_err_t fs_sd_card_init(const char* mount_point)
{
    return fs_sd_card_init_sdmmc(mount_point, NULL);
}