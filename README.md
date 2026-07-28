# Local Arch Vault 

A high-performance, local RAG (Retrieval-Augmented Generation) system designed to ingest, index, and query your Obsidian vault using an ultra-fast, entirely in-memory graph cache and a memory-mapped vector database. 

The backend has been migrated from Python to **native C++20** for maximum performance, minimal memory overhead, and direct hardware integration without relying on heavy external database engines.

---

## Architecture & Technology Stack

| Component | Technology | Description |
| :--- | :--- | :--- |
| **Web Framework** | **Crow** | High-performance C++ micro-framework handling REST API requests (`/query`, `/ingest`, `/sanitycheck`). |
| **Vector Database** | **USearch** *(or hnswlib)* | Header-only C++ library for semantic search. Uses `mmap` to load massive vector spaces directly from disk with near-zero startup time. |
| **Graph & Metadata** | **Native C++20** | Custom lightweight graph utilizing `std::unordered_map` to track documents, chunks, and wikilinks (`[[Note]]`). |
| **LLM Inference** | **llama.cpp** | Native C library integration running local models (e.g., Qwen2.5-Instruct GGUF) for embedding generation and answering. |
| **Parsing & Sync** | **Native C++ / Cereal** | Incremental delta-scanner using `std::filesystem` and `std::string_view` for efficient parsing, with Cereal for lightweight binary caching. |

---

## Persistence & Sync Strategy

Since the Obsidian `.md` files remain the ultimate source of truth, this system relies on a fast **derived index cache** rather than a traditional heavy database.

1. **Vector Persistence:** Embeddings are written to a contiguous binary file and loaded via `mmap`, bypassing the need to load gigabytes of vectors into RAM on startup.
2. **Metadata Delta-Sync:** On launch, the system scans the vault using `std::filesystem::recursive_directory_iterator`. It compares the `last_write_time` of each markdown file against a lightweight binary manifest.
3. **Hot Reloading:** Unchanged files are instantly loaded from the binary cache. Only new, modified, or deleted files trigger re-parsing and vector generation, making app restarts almost instantaneous.

---

## C++ Standards & Development Practices

This project targets **C++20** to leverage modern features like concepts, coroutines, and formatting libraries while ensuring strict memory safety and performance.

### Core Guidelines
1. **Modern RAII & Smart Pointers:** 
   * Raw `new` and `delete` are strictly prohibited. 
   * Use `std::unique_ptr` for exclusive resource ownership and `std::shared_ptr` only when shared ownership is required.
2. **Zero-Copy & Standard Libraries:**
   * Leverage `std::string_view` for efficient string manipulation during markdown and canvas parsing.
   * Use `std::filesystem` for robust, cross-platform file system traversal and delta checking.
3. **Asynchronicity:**
   * Utilize Crow's routing capabilities and C++20 paradigms to keep request handling responsive, separating CPU-bound graph/vector searches from the main I/O threads.

---

## Configuration

Copy `.env.example` to `.env` and adjust the configuration parameters to match your system specs and vault location:

```env
VAULT_PATH="~/path/to/your/vault"
VECTOR_INDEX_PATH="../data/vectors.usearch"
GRAPH_CACHE_PATH="../data/graph_manifest.bin"
MODEL_PATH="./models/qwen2.5-1.5b-instruct-q5_k_m.gguf"
N_CTX=4096
N_THREADS=4
VERBOSE=false
SIMILARITY_THRESHOLD=0.6
TEMPERATURE=0.1
