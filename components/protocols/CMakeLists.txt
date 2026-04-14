# 声明该组件
idf_component_register(
    # 列出所有 .cc 源文件
    SRCS 
        # protocol.cc
        # websocket_protocol.cc
        # mqtt_protocol.cc
    INCLUDE_DIRS 
        "."   # 当前目录，让头文件能被找到
    # 声明该组件对其他 ESP-IDF 组件的依赖
    REQUIRES 
        cjson
        esp-tls     # websocket_protocol 需要
        http_parser # websocket_protocol 需要
        espressif__mqtt    # mqtt_protocol 需要
        lwip
        freertos
)

# (可选) 如果该组件需要编译为 C++ 代码，可以取消下面的注释
# target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++17)