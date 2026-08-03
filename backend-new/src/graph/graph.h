#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

struct ChunkNode {
    std::string id;          
    std::string doc_path;    
    std::string header;
    size_t position = 0;         
    std::string text;        
    std::unordered_set<std::string> links_to;
};

struct DocumentNode {
    std::string path;        
    std::string title;       
    
    std::vector<std::string> tags;
    std::vector<std::string> aliases;
    
    std::vector<std::string> chunk_ids;         
};

class VaultGraph {
public:
    std::unordered_map<std::string, DocumentNode> documents;
    std::unordered_map<std::string, ChunkNode> chunks;

    void add_document(const DocumentNode& doc);
    void add_chunk(const ChunkNode& chunk);
    void add_link(const std::string& source_path, const std::string& target_title);

    void remove_document(const std::string& path);
    void remove_chunk(const std::string& id);

    bool resolve_and_add_link(const std::string& source_chunk_id, const std::string& target_doc_path, const std::string& target_header = "");
    std::vector<ChunkNode> get_budgeted_context(const std::string& seed_id, size_t max_chars) const;
    
    void clear();
};
