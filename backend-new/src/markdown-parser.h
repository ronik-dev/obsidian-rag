#pragma once

#include "parser.h"
#include <string>
#include <string_view>
#include <vector>

class MarkdownParser : public Parser {
public:
    std::vector<RawChunk> parse(std::string_view content, const ParserConfig& config) const override;
    
    // Extracts targets from [[Link]] or [[Link|Alias]] formats
    static std::vector<std::string> extract_wikilinks(std::string_view content);
};
