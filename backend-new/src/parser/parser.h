#pragma once
#include <string>
#include <string_view>
#include <vector>

struct RawChunk {
    std::string context_header;
    std::string text;
};

struct ParserConfig {
    size_t max_chars = 2000;
    std::string document_path = "Unknown";
};

class Parser {
public:
    virtual ~Parser() = default;
    
    virtual std::vector<RawChunk> parse(std::string_view content, const ParserConfig& config) const = 0;
};
