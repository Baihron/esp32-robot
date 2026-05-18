# 注册 ESP-IDF 组件
# 该组件为 mico_driver，提供麦克风 (MIC) 驱动功能
idf_component_register(
    # 指定组件的源文件
    SRCS "src/mico_driver.c"
    # 指定头文件搜索路径
    INCLUDE_DIRS "include"
    # 声明该组件依赖的其他组件
    REQUIRES driver
)