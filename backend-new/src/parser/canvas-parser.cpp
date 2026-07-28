#include "canvas-parser.h"
#include "markdown-parser.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::vector<RawChunk> CanvasParser::parse(std::string_view content, const ParserConfig& config) const {
    std::vector<RawChunk> chunks;
    
    // Contract fulfillment for completely empty files
    if (content.empty()) {
        chunks.push_back({"Top Level", "Empty Canvas\n"});
        return chunks;
    }

    try {
        json data = json::parse(content);
        if (!data.contains("nodes") || !data["nodes"].is_array()) {
            chunks.push_back({"Top Level", "Empty or Invalid Canvas Nodes\n"});
            return chunks;
        }

        MarkdownParser md_parser;

        for (const auto& node : data["nodes"]) {
            if (!node.contains("type") || !node["type"].is_string()) continue;
            
            std::string type = node["type"];
            
            if (type == "text" && node.contains("text") && node["text"].is_string()) {
                std::string text = node["text"];
                if (text.empty()) continue;
                
                // Canvas text nodes are markdown, so we route them through the MarkdownParser
                auto sub_chunks = md_parser.parse(text, config);
                for (auto& chunk : sub_chunks) {
                    // Prefix the context header to identify it as a canvas card
                    chunk.context_header = "Canvas Text Node: " + chunk.context_header;
                    chunks.push_back(chunk);
                }
            } 
            else if (type == "file" && node.contains("file") && node["file"].is_string()) {
                std::string file_ref = node["file"];
                chunks.push_back({"Canvas File Reference", "Links to file: " + file_ref + "\n"});
            } 
            else if (type == "link" && node.contains("url") && node["url"].is_string()) {
                std::string url = node["url"];
                chunks.push_back({"Canvas URL", "External Link: " + url + "\n"});
            }
        }
    } catch (const json::parse_error&) {
        chunks.push_back({"Top Level", "Failed to parse Canvas JSON.\n"});
    }

    // Contract fulfillment: if there are no valid nodes, return a fallback chunk
    if (chunks.empty()) {
        chunks.push_back({"Top Level", "No extractable content in Canvas.\n"});
    }

    return chunks;
}
