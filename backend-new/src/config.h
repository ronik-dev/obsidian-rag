#pragma once
#include <string>

struct Config {
    std::string vault_path;
    std::string db_path;
    std::string model_path;
    float similarity_threshold = 0.0f; 
};

class ConfigLoader {
public:
    static Config load(const std::string& filepath);
};
