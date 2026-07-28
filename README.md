# Local Arch Vault - C++ Backend

A high-performance, local RAG (Retrieval-Augmented Generation) system designed to ingest, index, and query your Obsidian vault using a graph-backed vector database and local LLM inference. 

The backend has been migrated from Python to **native C++** for optimized performance, lower memory overhead, and direct hardware integration.

---

## Architecture & Technology Stack

| Component | Technology | Description |
| :--- | :--- | :--- |
| **Web Framework** | **Crow** | High-performance C++ micro-framework for handling REST API requests (`/query`, `/ingest`, `/sanitycheck`). |
| **Graph & Vector DB** | **Kùzu (C++ API)** | Embedded graph database managing documents, folders, chunks, and semantic/wikilink relationships. |
| **LLM Inference** | **llama.cpp** | Native C library integration running local models (e.g., Qwen2.5-Instruct GGUF) for answer generation. |
| **Embeddings & Parsing** | *TBD / Native C++* | In-progress modules for text chunking (`std::filesystem`, `std::string_view`) and embedding generation. |

---

## C++ Standards & Development Practices

This project targets **C++20** to leverage modern features like concepts, coroutines, and formatting libraries while ensuring strict memory safety and performance.

### Core Guidelines
1. **Modern RAII & Smart Pointers:** 
   * Raw `new` and `delete` are strictly prohibited. 
   * Use `std::unique_ptr` for exclusive resource ownership and `std::shared_ptr` only when shared ownership is required.
2. **Zero-Copy & Standard Libraries:**
   * Leverage `std::string_view` for efficient string manipulation during markdown and canvas parsing.
   * Use `std::filesystem` for robust, cross-platform file system traversal.
3. **Asynchronicity:**
   * Utilize Crow's routing capabilities and C++20 paradigms to keep request handling responsive.

---

## Configuration

Copy `.env.example` to `.env` and adjust the configuration parameters to match your system specs and vault location:

```env
VAULT_PATH="~/path/to/your/vault"
DB_PATH="../data/vault.db"
MODEL_PATH="./models/qwen2.5-1.5b-instruct-q5_k_m.gguf"
N_CTX=4096
N_THREADS=4
VERBOSE=false
SIMILARITY_THRESHOLD=0.6
TEMPERATURE=0.1
