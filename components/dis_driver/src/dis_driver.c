#include "dis_driver.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "soc/soc.h"
#include <string.h>

static const char *TAG = "dis_driver";

static esp_lcd_panel_handle_t panel_handle = NULL;
static uint16_t *g_framebuffer = NULL;
static uint16_t g_width = 0;
static uint16_t g_height = 0;
static bool g_initialized = false;

// 初始化显示系统 - 只做硬件初始化和缓冲区分配
esp_err_t dis_driver_init(const dis_config_t *config)
{
    if (g_initialized) return ESP_OK;

    ESP_LOGI(TAG, "Initializing LCD hardware");

    // 保存尺寸
    g_width = config->width;
    g_height = config->height;

    // 初始化SPI总线
    spi_bus_config_t buscfg = {
        .sclk_io_num = config->sclk_gpio,
        .mosi_io_num = config->mosi_gpio,
        .miso_io_num = -1,
        .max_transfer_sz = g_width * g_height * sizeof(uint16_t),
        .flags = 0,
        .intr_flags = 0
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // 配置Panel IO
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = config->dc_gpio,
        .cs_gpio_num = config->cs_gpio,
        .pclk_hz = config->pclk_hz,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &io_handle));

    // 创建ST7789面板
    esp_lcd_panel_dev_config_t panel_dev_config = {
        .reset_gpio_num = config->rst_gpio,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_dev_config, &panel_handle));

    // 初始化面板
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // 分配帧缓冲区 - 确保32位对齐
    ESP_LOGI(TAG, "Allocating framebuffer");
    size_t buffer_size = g_width * g_height * sizeof(uint16_t);
    // 对齐到4字节
    g_framebuffer = heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (!g_framebuffer) {
        ESP_LOGE(TAG, "Failed to allocate framebuffer");
        return ESP_ERR_NO_MEM;
    }

    // 确保缓冲区清零
    memset(g_framebuffer, 0, buffer_size);

    g_initialized = true;
    ESP_LOGI(TAG, "LCD initialized: %dx%d, buffer at %p", g_width, g_height, g_framebuffer);
    return ESP_OK;
}

// 刷新屏幕到LCD
esp_err_t dis_flush(void)
{
    if (!g_initialized || !panel_handle || !g_framebuffer) {
        ESP_LOGE(TAG, "dis_flush failed: invalid state");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ESP_FAIL;
    ret = esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, g_width, g_height, g_framebuffer);
    vTaskDelay(pdMS_TO_TICKS(1));  // 短暂延迟后重试

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "dis_flush failed after retries: %s", esp_err_to_name(ret));
    }

    return ret;
}

// 获取帧缓冲区指针
uint16_t* dis_get_framebuffer(void)
{
    return g_framebuffer;
}

// 获取屏幕尺寸
void dis_get_size(uint16_t *width, uint16_t *height)
{
    if (width) *width = g_width;
    if (height) *height = g_height;
}

// 反初始化
void dis_driver_deinit(void)
{
    if (panel_handle) {
        esp_lcd_panel_del(panel_handle);
        panel_handle = NULL;
    }
    if (g_framebuffer) {
        free(g_framebuffer);
        g_framebuffer = NULL;
    }
    g_initialized = false;
}