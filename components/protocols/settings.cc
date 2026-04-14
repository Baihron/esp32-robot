#include "settings.h"

Settings::Settings(const std::string& ns, bool writeable) {
    // 空实现：不实际读写 NVS
}

int Settings::GetInt(const std::string& key, int default_value) {
    return default_value;  // 总是返回默认值
}

void Settings::SetInt(const std::string& key, int value) {
    // 空实现
}

std::string Settings::GetString(const std::string& key, const std::string& default_value) {
    return default_value;  // 总是返回默认值
}