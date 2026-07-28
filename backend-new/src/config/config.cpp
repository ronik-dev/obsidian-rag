#include "config.h"
#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>


Config ConfigLoader::load(const std::string& filepath){
    std::ifstream file(filepath);
    if(!file.is_open()) throw std::runtime_error("could not open file: " + filepath);
    
    nlohmann::json j;
    file >> j;

    Config config;
    config.vault_path = j.value("vault_path", "");
    config.db_path= j.value("db_path", "");
    config.model_path = j.value("model_path", "");
    config.similarity_threshold = j.value("similarity_threshold", 0.0f);
    
    return config;
}
