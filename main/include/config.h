#ifndef __CONFIG_H__
#define __CONFIG_H__

// ============================================
// 系统模块配置
// ============================================

// 启用/禁用系统模块
#define CAMERA_TASK             (1)    // 启用摄像头系统
#define DISPLAY_TASK            (1)    // 启用显示系统
#define SD_CARD                 (0)    // 启用SD卡存储
#define FACE_DETECT_TASK        (1)    // 启用人脸检测系统


// ============================================
// 系统配置
// ============================================
#define SYSTEM_TAG                     "APP"

// ============================================
// 摄像头任务配置
// ============================================
#define CAMERA_CAPTURE_TASK_TAG        "CAMERA_CAPTURE_TASK"
#define DEFAULT_CAPTURE_INTERVAL_MS    40      // 25 FPS (1000/40 = 25)
#define DEFAULT_CAPTURE_TASK_PRIORITY  (tskIDLE_PRIORITY + 2)
#define DEFAULT_CAPTURE_STACK_SIZE     4096
#define QUEUE_SEND_TIMEOUT_MS          100     // 发送队列超时
#define CONSECUTIVE_FAIL_THRESHOLD     10      // 连续失败阈值

// ============================================
// 显示任务配置
// ============================================
#define DISPLAY_TASK_TAG               "DISPLAY_TASK"
#define DEFAULT_DISPLAY_TASK_PRIORITY  (tskIDLE_PRIORITY + 3)
#define DEFAULT_DISPLAY_STACK_SIZE     4096
#define QUEUE_RECEIVE_TIMEOUT_MS       20      // 接收队列超时
#define QUEUE_BACKLOG_THRESHOLD        3       // 队列积压阈值

// ============================================
// 帧队列配置
// ============================================
#define FRAME_QUEUE_TAG                "FRAME_QUEUE"
#define DEFAULT_FRAME_QUEUE_SIZE       10       // 队列大小（帧数）
#define MAX_FRAME_SIZE                 (320*240*2)  // 最大帧大小

// ============================================
// 按钮任务配置
// ============================================
#define BUTTON_TASK_TAG                "BUTTON"

// ============================================
// 显示配置
// ============================================
// 摄像头原始分辨率
#define CAMERA_WIDTH                   320
#define CAMERA_HEIGHT                  240

// LCD分辨率
#define LCD_WIDTH                      240
#define LCD_HEIGHT                     240

// 裁剪参数（从320x240中裁剪中间240x240）
#define CROP_START_X                   ((CAMERA_WIDTH - LCD_WIDTH) / 2)  // 40
#define CROP_START_Y                   0

// ============================================
// 颜色定义
// ============================================
#define COLOR_BLACK                    0x0000
#define COLOR_WHITE                    0xFFFF
#define COLOR_RED                      0xF800
#define COLOR_GREEN                    0x07E0
#define COLOR_BLUE                     0x001F

// ============================================
// 系统监控配置
// ============================================
#define STATUS_PRINT_INTERVAL          100     // 状态打印间隔（秒）
#define TASK_STOP_CHECK_INTERVAL       1000    // 任务状态检查间隔（毫秒）

// ============================================
// 人脸识别任务配置
// ============================================
#define FACE_DETECT_TASK_TAG      "FACE_DETECT"
#define FACE_DETECT_TASK_PRIORITY (tskIDLE_PRIORITY + 1)
#define FACE_DETECTION_STACK_SIZE    8192

// ============================================
// 内存配置
// ============================================
#define MIN_FREE_HEAP_WARNING          10240   // 10KB警告阈值
#define CRITICAL_FREE_HEAP             5120    // 5KB临界阈值

// ============================================
// 调试配置
// ============================================
#ifdef CONFIG_DEBUG_MODE
    #define DEBUG_PRINT(fmt, ...)      ESP_LOGI("DEBUG", fmt, ##__VA_ARGS__)
    #define DEBUG_HEAP_CHECK()         ESP_LOGI("DEBUG", "Free heap: %d", esp_get_free_heap_size())
#else
    #define DEBUG_PRINT(fmt, ...)
    #define DEBUG_HEAP_CHECK()
#endif

// define CONFIG_DEBUG_PRINT


// ============================================
// 错误码定义
// ============================================
#define ERR_CAMERA_INIT_FAILED         0x1001
#define ERR_DISPLAY_INIT_FAILED        0x1002
#define ERR_QUEUE_INIT_FAILED          0x1003
#define ERR_TASK_CREATE_FAILED         0x1004
#define ERR_MEMORY_ALLOC_FAILED        0x1005

// ============================================
// 性能监控
// ============================================
#define PERFORMANCE_SAMPLING_RATE      30     // 每10秒采样一次性能数据

#endif /* __CONFIG_H__ */