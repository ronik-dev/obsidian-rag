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
    ParserConfig config{.max_chars = 150, .document_path = "Test.md"}; 
    
    std::string input = 
        "This is a very long line that is designed to be exceptionally long so that "
        "when combined with the prefix, it forces the parser to split it up to "
        "respect the strict limits of the vector database.";
        
    auto chunks = parser.parse(input, config);
    
    for (const auto& chunk : chunks) {
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

    TEST_CASE("Extracts YAML frontmatter and prepends metadata to chunks") {
        MarkdownParser parser;
        ParserConfig config{.max_chars = 2000, .document_path = "Dev/Linux_Notes.md"};

        std::string input = 
            "---\n"
            "tags:\n"
            "  - Dev/Phase\n"
            "  - Dev/Linux\n"
            "aliases:\n"
            "  - secret management\n"
            "  - key management\n"
            "---\n"
            "# Deployment\n"
            "Run the deployment script.\n";

        auto chunks = parser.parse(input, config);

        REQUIRE(chunks.size() == 1);

        // Check that the injected context block exists at the top of the text
        CHECK(chunks[0].text.find("[File: Dev/Linux_Notes.md]") != std::string::npos);
        CHECK(chunks[0].text.find("[Tags: Dev/Phase, Dev/Linux]") != std::string::npos);
        CHECK(chunks[0].text.find("[Aliases: secret management, key management]") != std::string::npos);
        CHECK(chunks[0].text.find("[Section: Deployment]") != std::string::npos);

        // Ensure the raw content is still there
        CHECK(chunks[0].text.find("Run the deployment script.") != std::string::npos);
    }

    TEST_CASE("Handles markdown without frontmatter gracefully") {
        MarkdownParser parser;
        ParserConfig config{.max_chars = 2000, .document_path = "Simple.md"};

        std::string input = 
            "# Simple File\n"
            "Just some standard text.\n";

        auto chunks = parser.parse(input, config);

        REQUIRE(chunks.size() == 1);

        // It should still inject the file path
        CHECK(chunks[0].text.find("File: Simple.md") != std::string::npos);

        // It should NOT contain tags or aliases labels
        CHECK(chunks[0].text.find("Tags:") == std::string::npos);
        CHECK(chunks[0].text.find("Aliases:") == std::string::npos);

        CHECK(chunks[0].text.find("Just some standard text.") != std::string::npos);
    }


}

TEST_SUITE("CanvasParser Specifics") {

    TEST_CASE("Correctly routes text nodes through the MarkdownParser") {
        CanvasParser parser;
        ParserConfig config{.max_chars = 2000, .document_path = "MyBoard.canvas"};

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
        
        // Use .find() since the text now contains the [File: ...] and [Section: ...] prefix
        CHECK(chunks[0].text.find("[File: MyBoard.canvas]") != std::string::npos);
        CHECK(chunks[0].text.find("[Section: Canvas Header]") != std::string::npos);
        CHECK(chunks[0].text.find("Some markdown text.") != std::string::npos);
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
