#include "markdown-parser.h"
#include <sstream>
#include <algorithm>
#include <regex>

namespace {
    // =========================================================
    // 1. String Utility Helpers
    // =========================================================

    bool is_whitespace_only(const std::string& str) {
        return std::all_of(str.begin(), str.end(), [](unsigned char c) { return std::isspace(c); });
    }

    void replace_all(std::string& str, const std::string& from, const std::string& to) {
        size_t start_pos = 0;
        while((start_pos = str.find(from, start_pos)) != std::string::npos) {
            str.replace(start_pos, from.length(), to);
            start_pos += to.length();
        }
    }

    std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        size_t end = s.find_last_not_of(" \t\r\n");
        return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
    }

    // =========================================================
    // 2. YAML Frontmatter Extraction
    // =========================================================

    struct Frontmatter {
        std::vector<std::string> tags;
        std::vector<std::string> aliases;
        std::string body;
    };

    Frontmatter extract_frontmatter(const std::string& text) {
        Frontmatter fm;
        std::stringstream fs(text);
        std::string f_line;
        bool in_frontmatter = false;
        bool is_first_line = true;
        std::string current_list = "";

        while (std::getline(fs, f_line)) {
            if (is_first_line && f_line.rfind("---", 0) == 0) {
                in_frontmatter = true;
                is_first_line = false;
                continue;
            }
            is_first_line = false;

            if (in_frontmatter) {
                if (f_line.rfind("---", 0) == 0) { 
                    in_frontmatter = false;
                    continue;
                }
                if (f_line.rfind("tags:", 0) == 0) { current_list = "tags"; continue; }
                if (f_line.rfind("aliases:", 0) == 0) { current_list = "aliases"; continue; }

                size_t dash_pos = f_line.find("- ");
                if (dash_pos != std::string::npos) {
                    std::string val = trim(f_line.substr(dash_pos + 2));
                    if (!val.empty()) {
                        if (current_list == "tags") fm.tags.push_back(val);
                        else if (current_list == "aliases") fm.aliases.push_back(val);
                    }
                } else if (!f_line.empty() && !std::isspace(f_line[0]) && f_line.find(':') != std::string::npos) {
                    current_list = ""; 
                }
            } else {
                fm.body += f_line + "\n";
            }
        }
        return fm;
    }

    // =========================================================
    // 3. Context Prefix Generator
    // =========================================================

    std::string build_context_prefix(const std::string& document_path, const Frontmatter& fm, const std::string& heading) {
        std::string prefix = "[File: " + document_path + "]\n";

        if (!fm.tags.empty()) {
            prefix += "[Tags: ";
            for (size_t i = 0; i < fm.tags.size(); ++i) prefix += fm.tags[i] + (i + 1 < fm.tags.size() ? ", " : "");
            prefix += "]\n";
        }

        if (!fm.aliases.empty()) {
            prefix += "[Aliases: ";
            for (size_t i = 0; i < fm.aliases.size(); ++i) prefix += fm.aliases[i] + (i + 1 < fm.aliases.size() ? ", " : "");
            prefix += "]\n";
        }

        prefix += "[Section: " + heading + "]\n---\n";
        return prefix;
    }

} // End anonymous namespace


// =========================================================
// Main Implementation
// =========================================================

std::vector<RawChunk> MarkdownParser::parse(std::string_view content, const ParserConfig& config) const {
    std::string text_copy(content);
    replace_all(text_copy, "\xC2\xA0", " "); 

    Frontmatter fm = extract_frontmatter(text_copy);

    std::vector<RawChunk> chunks;
    std::string current_heading = "Top Level";
    std::string current_text;

    std::string current_prefix = build_context_prefix(config.document_path, fm, current_heading);
    size_t current_length = current_prefix.length();

    std::stringstream ss(fm.body);
    std::string line;

    // Helper lambda to finalize and store a chunk
    auto push_chunk = [&](bool continuation) {
        if (!current_text.empty() && !is_whitespace_only(current_text)) {
            chunks.push_back({current_heading, current_prefix + current_text});
        }
        if (continuation && current_heading.find("(Cont.)") == std::string::npos) {
            current_heading += " (Cont.)";
        }
        current_text.clear();
        current_prefix = build_context_prefix(config.document_path, fm, current_heading);
        current_length = current_prefix.length();
    };

    while (std::getline(ss, line)) {
        // Detect Headings
        if (line.rfind("# ", 0) == 0 || line.rfind("## ", 0) == 0 || line.rfind("### ", 0) == 0) {
            push_chunk(false);

            size_t first_char = line.find_first_not_of('#');
            if (first_char != std::string::npos) {
                size_t start = line.find_first_not_of(' ', first_char);
                current_heading = (start != std::string::npos) ? line.substr(start) : "Section";
            }

            current_prefix = build_context_prefix(config.document_path, fm, current_heading);
            current_text = line + "\n";
            current_length = current_prefix.length() + current_text.length();
        } 
        else {
            // Hard split if max_chars exceeded
            if (current_length + line.length() + 1 > config.max_chars && !current_text.empty()) {
                push_chunk(true);
            }

            // Hard split loop for exceptionally long single lines
            std::string remaining_line = line;

            // Added check: !remaining_line.empty() prevents infinite loops if prefix > max_chars
            while (current_prefix.length() + remaining_line.length() + 1 > config.max_chars && !remaining_line.empty()) {

                size_t capacity = (config.max_chars > current_prefix.length() + 10) 
                    ? (config.max_chars - current_prefix.length() - 1) 
                    : 10; // Fallback capacity if prefix is massive

                // Capping the capacity prevents the out_of_range substr exception
                capacity = std::min(capacity, remaining_line.length());

                current_text += remaining_line.substr(0, capacity) + "\n";
                push_chunk(true);
                remaining_line = remaining_line.substr(capacity);
            }

            current_text += remaining_line + "\n";
            current_length = current_prefix.length() + current_text.length();
        }
    }

    push_chunk(false);
    return chunks;
}

std::vector<std::string> MarkdownParser::extract_wikilinks(std::string_view content) {
    std::vector<std::string> links;
    static const std::regex wikilink_regex(R"(\[\[(.*?)(?:\|.*?)?\]\])");
    std::string text(content);

    for (std::sregex_iterator i = std::sregex_iterator(text.begin(), text.end(), wikilink_regex); i != std::sregex_iterator(); ++i) {
        if (i->size() > 1) links.push_back((*i)[1].str());
    }
    return links;
}
