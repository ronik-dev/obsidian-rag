#include "doctest.h"
#include <fstream>
#include <filesystem>
#include "../src/config.h"

TEST_CASE("ConfigLoader successfully parses JSON configuration") {
    // 1. Setup: Create a temporary mock JSON file
    std::string test_filepath = "test_config.json";
    std::ofstream out(test_filepath);
    out << R"({
        "vault_path": "/mock/vault",
        "db_path": "/mock/data/vault.db",
        "model_path": "/mock/models/model.gguf",
        "similarity_threshold": 0.65
    })";
    out.close();

    // 2. Action: Attempt to load the configuration
    Config config = ConfigLoader::load(test_filepath);

    // 3. Assert: Verify the struct was populated correctly
    CHECK(config.vault_path == "/mock/vault");
    CHECK(config.db_path == "/mock/data/vault.db");
    CHECK(config.model_path == "/mock/models/model.gguf");
    CHECK(config.similarity_threshold == doctest::Approx(0.65));

    // 4. Teardown: Clean up the test file
    std::filesystem::remove(test_filepath);
}
