#include "markdown-parser.h"
#include <sstream>
#include <algorithm>
#include <regex>

// Internal helper for whitespace validation
static bool is_whitespace_only(const std::string& str) {
    return std::all_of(str.begin(), str.end(), [](unsigned char c) { return std::isspace(c); });
}

// Internal helper to replace substrings
static void replace_all(std::string& str, const std::string& from, const std::string& to) {
    size_t start_pos = 0;
    while((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
}

std::vector<RawChunk> MarkdownParser::parse(std::string_view content, const ParserConfig& config) const {
    std::vector<RawChunk> chunks;
    
    // Clean non-breaking spaces (\xC2\xa0)
    std::string text_copy(content);
    replace_all(text_copy, "\xC2\xA0", " "); 

    std::string current_heading = "Top Level";
    std::string current_text;
    size_t current_length = 0;
    
    std::stringstream ss(text_copy);
    std::string line;

    auto push_chunk = [&](bool continuation) {
        if (!current_text.empty() && !is_whitespace_only(current_text)) {
            chunks.push_back({current_heading, current_text});
        }
        if (continuation && current_heading.find("(Cont.)") == std::string::npos) {
            current_heading += " (Cont.)";
        }
        current_text.clear();
        current_length = 0;
    };

    while (std::getline(ss, line)) {
        // Strict heading detection: hashes followed by a space
        if (line.rfind("# ", 0) == 0 || line.rfind("## ", 0) == 0 || line.rfind("### ", 0) == 0) {
            push_chunk(false);
            
            // Extract the heading title
            size_t first_char = line.find_first_not_of('#');
            if (first_char != std::string::npos) {
                size_t start = line.find_first_not_of(' ', first_char);
                current_heading = (start != std::string::npos) ? line.substr(start) : "Section";
            }
            current_text = line + "\n";
            current_length = line.length() + 1;
        } else {
            // 1. If adding the line exceeds the limit AND we already have text, flush it
            if (current_length + line.length() > config.max_chars && !current_text.empty()) {
                push_chunk(true);
            }
            
            // 2. If a single line is absurdly long (longer than the config limit on its own), 
            // we must slice it into pieces to respect the strict contract.
            std::string remaining_line = line;
            while (remaining_line.length() > config.max_chars) {
                current_text += remaining_line.substr(0, config.max_chars);
                push_chunk(true);
                remaining_line = remaining_line.substr(config.max_chars);
            }
            
            // 3. Append whatever is left of the line (now guaranteed to be < max_chars)
            current_text += remaining_line + "\n";
            current_length = current_text.length();
        }
    }
    
    push_chunk(false);
    return chunks;
}

std::vector<std::string> MarkdownParser::extract_wikilinks(std::string_view content) {
    std::vector<std::string> links;
    // Matches [[Link]] or [[Link|Alias]] capturing only the 'Link' portion
    static const std::regex wikilink_regex(R"(\[\[(.*?)(?:\|.*?)?\]\])");
    
    std::string text(content);
    auto words_begin = std::sregex_iterator(text.begin(), text.end(), wikilink_regex);
    auto words_end = std::sregex_iterator();

    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        std::smatch match = *i;
        if (match.size() > 1) {
            links.push_back(match[1].str());
        }
    }
    return links;
}
