#pragma once
#include "parser.h"
#include <string>
#include <string_view>
#include <vector>

class CanvasParser : public Parser {
public:
    std::vector<RawChunk> parse(std::string_view content, const ParserConfig& config) const override;
};
