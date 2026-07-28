// src/ParserFactory.h
#pragma once

#include "parser.h"
#include "markdown-parser.h"
#include "canvas-parser.h"
#include <memory>
#include <string_view>
#include <stdexcept>

class ParserFactory {
public:
    // Factory method returning a polymorphic smart pointer
    static std::unique_ptr<parser> create(std::string_view filepath) {
        if (filepath.ends_with(".md")) {
            return std::make_unique<MarkdownParser>();
        } 
        if (filepath.ends_with(".canvas")) {
            // return std::make_unique<CanvasParser>();
        }
        
        // Throw or return nullptr for unsupported types
        throw std::invalid_argument("Unsupported file extension.");
    }
};
