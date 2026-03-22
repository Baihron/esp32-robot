#include "flash_driver.h"
#include "esp_partition.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "FACE_FLASH";

// 分区名称（与 partitions.csv 中一致）
#define PARTITION_NAME      "face_img"
#define PARTITION_SUBTYPE   0xff

// 每个槽位大小：1MB / 4 = 256KB（可根据实际图片大小调整）
#define SLOT_SIZE           (256 * 1024)

// 每个槽位头部：存储实际图片大小（4字节）
#define SLOT_HEADER_SIZE    4

// Flash 擦除块大小（4KB）
#define ERASE_BLOCK_SIZE    4096

static const esp_partition_t *s_partition = NULL;
static int s_max_slots = 0;

esp_err_t face_flash_storage_init(void)
{
    s_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        PARTITION_SUBTYPE,
        PARTITION_NAME
    );

    if (s_partition == NULL) {
        ESP_LOGE(TAG, "Partition '%s' not found! Check partitions.csv.", PARTITION_NAME);
        return ESP_ERR_NOT_FOUND;
    }

    s_max_slots = s_partition->size / SLOT_SIZE;
    ESP_LOGI(TAG, "Flash storage init OK. Partition size: %lu bytes, max slots: %d",
             s_partition->size, s_max_slots);

    return ESP_OK;
}

int face_flash_storage_max_slots(void)
{
    return s_max_slots;
}

esp_err_t face_flash_storage_save(const uint8_t *data, size_t size, int slot)
{
    if (s_partition == NULL) {
        ESP_LOGE(TAG, "Not initialized. Call face_flash_storage_init() first.");
        return ESP_ERR_INVALID_STATE;
    }
    if (data == NULL || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (slot < 0 || slot >= s_max_slots) {
        ESP_LOGE(TAG, "Invalid slot %d (max: %d)", slot, s_max_slots - 1);
        return ESP_ERR_INVALID_ARG;
    }
    if (size + SLOT_HEADER_SIZE > SLOT_SIZE) {
        ESP_LOGE(TAG, "Image too large: %zu bytes (max: %d)", size, SLOT_SIZE - SLOT_HEADER_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    size_t slot_offset = slot * SLOT_SIZE;

    // 1. 擦除该槽位（按 4KB 对齐）
    size_t erase_size = ((SLOT_SIZE + ERASE_BLOCK_SIZE - 1) / ERASE_BLOCK_SIZE) * ERASE_BLOCK_SIZE;
    esp_err_t ret = esp_partition_erase_range(s_partition, slot_offset, erase_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erase failed: %d", ret);
        return ret;
    }

    // 2. 写入头部（图片大小）
    uint32_t img_size = (uint32_t)size;
    ret = esp_partition_write(s_partition, slot_offset, &img_size, sizeof(img_size));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Write header failed: %d", ret);
        return ret;
    }

    // 3. 写入图片数据
    ret = esp_partition_write(s_partition, slot_offset + SLOT_HEADER_SIZE, data, size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Write data failed: %d", ret);
        return ret;
    }

    ESP_LOGI(TAG, "Saved %zu bytes to slot %d (offset: 0x%x)", size, slot, slot_offset);
    return ESP_OK;
}

esp_err_t face_flash_storage_load(int slot, uint8_t *buf, size_t buf_size, size_t *out_size)
{
    if (s_partition == NULL) {
        ESP_LOGE(TAG, "Not initialized.");
        return ESP_ERR_INVALID_STATE;
    }
    if (buf == NULL || out_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (slot < 0 || slot >= s_max_slots) {
        ESP_LOGE(TAG, "Invalid slot %d", slot);
        return ESP_ERR_INVALID_ARG;
    }

    size_t slot_offset = slot * SLOT_SIZE;

    // 1. 读取头部（图片大小）
    uint32_t img_size = 0;
    esp_err_t ret = esp_partition_read(s_partition, slot_offset, &img_size, sizeof(img_size));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read header failed: %d", ret);
        return ret;
    }

    if (img_size == 0 || img_size == 0xFFFFFFFF) {
        ESP_LOGW(TAG, "Slot %d is empty or erased.", slot);
        *out_size = 0;
        return ESP_ERR_NOT_FOUND;
    }

    if (img_size > buf_size) {
        ESP_LOGE(TAG, "Buffer too small: need %lu, got %zu", img_size, buf_size);
        return ESP_ERR_INVALID_SIZE;
    }

    // 2. 读取图片数据
    ret = esp_partition_read(s_partition, slot_offset + SLOT_HEADER_SIZE, buf, img_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read data failed: %d", ret);
        return ret;
    }

    *out_size = img_size;
    ESP_LOGI(TAG, "Loaded %lu bytes from slot %d", img_size, slot);
    return ESP_OK;
}

esp_err_t face_flash_storage_erase(int slot)
{
    if (s_partition == NULL) return ESP_ERR_INVALID_STATE;
    if (slot < 0 || slot >= s_max_slots) return ESP_ERR_INVALID_ARG;

    size_t slot_offset = slot * SLOT_SIZE;
    size_t erase_size = ((SLOT_SIZE + ERASE_BLOCK_SIZE - 1) / ERASE_BLOCK_SIZE) * ERASE_BLOCK_SIZE;

    esp_err_t ret = esp_partition_erase_range(s_partition, slot_offset, erase_size);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Slot %d erased.", slot);
    }
    return ret;
}

esp_err_t face_flash_storage_erase_all(void)
{
    if (s_partition == NULL) return ESP_ERR_INVALID_STATE;

    esp_err_t ret = esp_partition_erase_range(s_partition, 0, s_partition->size);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "All slots erased.");
    }
    return ret;
}