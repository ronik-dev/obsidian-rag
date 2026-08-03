#include "graph.h"
#include <algorithm>

void VaultGraph::add_document(const DocumentNode& doc) {
    documents[doc.path] = doc;
}

void VaultGraph::add_chunk(const ChunkNode& chunk) {
    chunks[chunk.id] = chunk;
    documents[chunk.doc_path].chunk_ids.push_back(chunk.id);
}

void VaultGraph::remove_chunk(const std::string& id) {
    auto chunk_it = chunks.find(id);
    if (chunk_it == chunks.end()) return;

    // Remove the chunk ID from the parent document's array
    std::string parent_path = chunk_it->second.doc_path;
    auto doc_it = documents.find(parent_path);
    if (doc_it != documents.end()) {
        auto& c_ids = doc_it->second.chunk_ids;
        c_ids.erase(std::remove(c_ids.begin(), c_ids.end(), id), c_ids.end());
    }

    chunks.erase(chunk_it);
}

void VaultGraph::remove_document(const std::string& path) {
    auto doc_it = documents.find(path);
    if (doc_it == documents.end()) return;

    // Cascade: Orphaned chunks must be deleted from RAM
    for (const auto& chunk_id : doc_it->second.chunk_ids) {
        chunks.erase(chunk_id);
    }

    documents.erase(doc_it);
}

bool VaultGraph::resolve_and_add_link(const std::string& source_chunk_id, const std::string& target_doc_path, const std::string& target_header) {
    auto source_it = chunks.find(source_chunk_id);
    auto doc_it = documents.find(target_doc_path);
    
    if (source_it == chunks.end() || doc_it == documents.end()) return false;
    
    const auto& target_c_ids = doc_it->second.chunk_ids;
    if (target_c_ids.empty()) return false;

    // If no specific header was requested, link to the top of the file (position 0)
    if (target_header.empty()) {
        source_it->second.links_to.insert(target_c_ids[0]);
        return true;
    }

    // PASS 1: Look for an exact match on the parsed context header
    for (const auto& c_id : target_c_ids) {
        if (chunks[c_id].header == target_header) {
            source_it->second.links_to.insert(c_id);
            return true;
        }
    }

    // PASS 2: Fallback to substring matching within the raw text body
    for (const auto& c_id : target_c_ids) {
        if (chunks[c_id].text.find(target_header) != std::string::npos) {
            source_it->second.links_to.insert(c_id);
            return true;
        }
    }

    return false;
}

std::vector<ChunkNode> VaultGraph::get_budgeted_context(const std::string& seed_id, size_t max_chars) const {
    std::vector<ChunkNode> result;
    auto seed_it = chunks.find(seed_id);
    if (seed_it == chunks.end()) return result;
    
    const ChunkNode& seed_chunk = seed_it->second;
    
    std::unordered_set<std::string> visited;
    size_t current_chars = 0;
    
    // 1. Prioritize the Seed Chunk
    result.push_back(seed_chunk);
    visited.insert(seed_id);
    current_chars += seed_chunk.text.length();

    // 2. Build our priority queue of candidates (Spatial Neighbors first, explicit Links second)
    std::vector<std::string> candidates;
    
    auto doc_it = documents.find(seed_chunk.doc_path);
    if (doc_it != documents.end()) {
        const auto& c_ids = doc_it->second.chunk_ids;
        auto it = std::find(c_ids.begin(), c_ids.end(), seed_id);
        
        if (it != c_ids.end()) {
            size_t center_idx = std::distance(c_ids.begin(), it);
            size_t max_radius = 2; // Grab up to 2 chunks before and 2 after
            
            // Expand outward symmetrically: +1, -1, +2, -2
            for (size_t d = 1; d <= max_radius; ++d) {
                if (center_idx + d < c_ids.size()) candidates.push_back(c_ids[center_idx + d]);
                if (center_idx >= d) candidates.push_back(c_ids[center_idx - d]);
            }
        }
    }
    
    // Append Explicit Links to candidates
    for (const auto& linked_id : seed_chunk.links_to) {
        candidates.push_back(linked_id);
    }
    
    // 3. Consume candidates until budget is exhausted
    for (const auto& candidate_id : candidates) {
        if (visited.count(candidate_id)) continue;
        
        auto cand_it = chunks.find(candidate_id);
        if (cand_it != chunks.end()) {
            size_t length = cand_it->second.text.length();
            
            // Terminate BFS if adding this chunk exceeds the LLM context limit
            if (current_chars + length > max_chars) {
                break; 
            }
            
            result.push_back(cand_it->second);
            visited.insert(candidate_id);
            current_chars += length;
        }
    }
    
    return result;
}

void VaultGraph::clear() {
    documents.clear();
    chunks.clear();
}
