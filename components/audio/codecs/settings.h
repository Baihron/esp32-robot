#pragma once
#include <string>

class Settings {
public:
    Settings(const std::string& ns, bool writeable = false);
    int GetInt(const std::string& key, int default_value);
    void SetInt(const std::string& key, int value);
};