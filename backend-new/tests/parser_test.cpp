#include "doctest.h"
#include "../src/parser/markdown-parser.h"
#include "../src/parser/canvas-parser.h" 

// =====================================================================
// 1. GENERIC PARSER CONTRACT (Runs for ALL parser types)
// =====================================================================

// We tell doctest to run these tests for the types listed here.
// As you build new parsers, you simply add them to this list!
#define PARSER_TYPES MarkdownParser /*, CanvasParser, PdfParser */

TEST_CASE_TEMPLATE("Contract: Parsers must never return chunks exceeding max_chars", T, PARSER_TYPES) {
    T parser;
    ParserConfig config{.max_chars = 10}; 
    
    std::string input = "This is a very long line that must be split to respect the strict limits.";
    auto chunks = parser.parse(input, config);
    
    for (const auto& chunk : chunks) {
        // Accounting for potential added newlines by the parser implementations
        CHECK(chunk.text.length() <= config.max_chars + 1); 
    }
}

TEST_CASE_TEMPLATE("Contract: Parsers must never return empty or whitespace-only chunks", T, PARSER_TYPES) {
    T parser;
    ParserConfig config{.max_chars = 100};
    
    std::string input = "\n   \n\t\nValid Text\n   \n";
    auto chunks = parser.parse(input, config);
    
    REQUIRE(chunks.size() == 1);
    CHECK_FALSE(chunks[0].text.empty());
    // Ensure it's not just whitespace
    CHECK(chunks[0].text.find_first_not_of(" \n\r\t") != std::string::npos);
}

TEST_CASE_TEMPLATE("Contract: Parsers must assign a fallback context if none is found", T, PARSER_TYPES) {
    T parser;
    ParserConfig config{.max_chars = 100};
    
    std::string input = "Just some text without any headers or explicit context.";
    auto chunks = parser.parse(input, config);
    
    REQUIRE(chunks.size() == 1);
    CHECK_FALSE(chunks[0].context_header.empty()); 
}


// =====================================================================
// 2. SPECIFIC PARSER BEHAVIORS
// =====================================================================

TEST_SUITE("MarkdownParser Specifics") {
    
    TEST_CASE("Splits correctly by Obsidian headings") {
        MarkdownParser parser;
        ParserConfig config{.max_chars = 2000};
        
        std::string input = 
            "# Introduction\n"
            "This is intro.\n"
            "## Details\n"
            "These are details.\n";
            
        auto chunks = parser.parse(input, config);
        
        REQUIRE(chunks.size() == 2);
        CHECK(chunks[0].context_header == "Introduction");
        CHECK(chunks[1].context_header == "Details");
    }

    TEST_CASE("Appends (Cont.) when hard-splitting long sections") {
        MarkdownParser parser;
        ParserConfig config{.max_chars = 20}; 
        
        std::string input = 
            "## A Long Section\n"
            "This line is short.\n"
            "This line forces a chunk boundary split.\n";
            
        auto chunks = parser.parse(input, config);
        
        REQUIRE(chunks.size() >= 2);
        CHECK(chunks[0].context_header == "A Long Section");
        CHECK(chunks[1].context_header == "A Long Section (Cont.)");
    }
    
    TEST_CASE("Extracts Obsidian Wikilinks") {
        std::string input = "Check out [[Vault Architecture]] and [[C++ Migration|this page]].";
        auto links = MarkdownParser::extract_wikilinks(input);
        
        REQUIRE(links.size() == 2);
        CHECK(links[0] == "Vault Architecture");
        CHECK(links[1] == "C++ Migration");
    }
}

TEST_SUITE("CanvasParser Specifics") {
    
    TEST_CASE("Correctly routes text nodes through the MarkdownParser") {
        CanvasParser parser;
        ParserConfig config{.max_chars = 2000};
        
        std::string json_input = R"({
            "nodes": [
                {
                    "type": "text",
                    "text": "# Canvas Header\nSome markdown text."
                }
            ]
        })";
        
        auto chunks = parser.parse(json_input, config);
        
        REQUIRE(chunks.size() == 1);
        CHECK(chunks[0].context_header == "Canvas Text Node: Canvas Header");
        CHECK(chunks[0].text == "# Canvas Header\nSome markdown text.\n");
    }

    TEST_CASE("Correctly extracts file and link nodes") {
        CanvasParser parser;
        ParserConfig config{.max_chars = 2000};
        
        std::string json_input = R"({
            "nodes": [
                {"type": "file", "file": "architecture.png"},
                {"type": "link", "url": "https://cppreference.com"}
            ]
        })";
        
        auto chunks = parser.parse(json_input, config);
        
        REQUIRE(chunks.size() == 2);
        CHECK(chunks[0].context_header == "Canvas File Reference");
        CHECK(chunks[0].text == "Links to file: architecture.png\n");
        CHECK(chunks[1].context_header == "Canvas URL");
        CHECK(chunks[1].text == "External Link: https://cppreference.com\n");
    }
}
