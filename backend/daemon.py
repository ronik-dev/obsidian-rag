import logging
from contextlib import asynccontextmanager
from fastapi import FastAPI
from fastapi.responses import StreamingResponse
from pydantic import BaseModel
from urllib.parse import quote 
import kuzu
import os
from sentence_transformers import SentenceTransformer
from llama_cpp import Llama
from sanity_check import check
import subprocess


logging.basicConfig(level=logging.INFO)

# Global State
db = None
conn = None
llm = None
embedder = None
DB_PATH = os.getenv('DB_PATH', '../data/vault.db')
VAULT_NAME = os.getenv('VAULT_NAME', 'LocalArchVault')

# Configuration -- llamacpp
MODEL_PATH = os.getenv('MODEL_PATH', 'models/qwen2.5-1.5b-instruct-q4_k_m.gguf')
N_CTX = int(os.getenv('N_CTX', '4096'))                                     # Context window size                                   
N_THREADS = int(os.getenv('N_THREADS', '4'))                                # Adjust based on your CPU
VERBOSE = os.getenv('VERBOSE', 'False').lower() in ('true', '1', 't')       # Keeps terminal output clean 

# Configuration -- custom
SIMILARITY_THRESHOLD = float(os.getenv('SIMILARITY_THRESHOLD', '0.3'))      # (0.3 is a safe baseline for all-MiniLM-L6-v2)
TEMPERATURE = float(os.getenv('TEMPERATURE', '0.1'))                        # is suggested to keep the temperature low
MAX_TOKENS = int(os.getenv('MAX_TOKENS', '1024'))
MAX_CONTEXT_CHARS = 12000                                                   # safe for default 4096 context

@asynccontextmanager
async def lifespan(app: FastAPI):
    global db, conn, llm, embedder
    
    logging.info("Loading KùzuDB...")
    db = kuzu.Database(DB_PATH, read_only=True)
    conn = kuzu.Connection(db)
    
    logging.info("Loading Qwen model into RAM...")
    # Make sure this matches the exact filename you downloaded
    llm = Llama(
        model_path=MODEL_PATH,
        n_ctx=N_CTX,
        n_threads=N_THREADS,
        verbose=VERBOSE
    )
    
    logging.info("Loading Embedding Model...")
    embedder = SentenceTransformer('all-MiniLM-L6-v2')
    logging.info("Daemon is warm and ready.")
    yield
    
    logging.info("Shutting down daemon...")
    # Kuzu connections close automatically on garbage collection

app = FastAPI(lifespan=lifespan)

class QueryRequest(BaseModel):
    query: str

@app.get("/sanitycheck")
async def handle_sanity_check():
    """Returns the current count of nodes and edges in the database."""
    try:
        docs = conn.execute("MATCH (d:Document) RETURN count(d)").get_next()[0]
        chunks = conn.execute("MATCH (c:Chunk) RETURN count(c)").get_next()[0]
        part_of = conn.execute("MATCH ()-[e:PART_OF]->() RETURN count(e)").get_next()[0]
        
        return {
            "status": "success",
            "counts": {
                "Documents": docs,
                "Chunks": chunks,
                "PART_OF_edges": part_of
            }
        }
    except Exception as e:
        logging.error(f"Sanity check failed: {e}")
        return {"status": "error", "message": str(e)}

@app.post("/ingest")
async def handle_ingest():
    """Triggers the ingestion script in the background."""
    try:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        
        subprocess.Popen(
            ["uv", "run", "ingest.py"],
            cwd=script_dir,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
        
        return {
            "status": "success", 
            "message": "Ingestion started in the background. Run /sanitycheck in a few moments to see the updated counts."
        }
    except Exception as e:
        logging.error(f"Failed to start ingestion: {e}")
        return {"status": "error", "message": str(e)}

@app.post("/query")
async def handle_query(req: QueryRequest):
    query_embedding = embedder.encode(req.query).tolist()

    # GRAPH-AWARE CYPHER QUERY
    # We retrieve the matching chunk, its parent document, 
    # and optional 1-hop outgoing/incoming Wikilinks for extra context.
    cypher_query = """
        MATCH (c:Chunk)-[:PART_OF]->(d:Document)
        WITH c, d, array_cosine_similarity(c.embedding, $q_embed) AS sim
        WHERE sim > $threshold
        
        OPTIONAL MATCH (other:Chunk)-[:PART_OF]->(d)
        WHERE other.position >= c.position - 2
          AND other.position <= c.position + 2
          AND other.id <> c.id
          
        WITH c, d, sim, collect({
            position: other.position,
            heading: other.heading,
            text: other.text
        }) AS context_chunks
        
        OPTIONAL MATCH (d)-[:LINKS_TO]-(linked:Document)
        
        RETURN
            d.title,
            d.path,
            c.position,
            c.heading,
            c.text,
            sim,
            context_chunks,
            collect(DISTINCT linked.title) AS linked_notes
        ORDER BY sim DESC
        LIMIT 15
        """
    
    results = conn.execute(cypher_query, {
        "q_embed": query_embedding,
        "threshold": SIMILARITY_THRESHOLD
    })
    
    context_blocks = []
    doc_index = 1

    encoded_vault = quote(VAULT_NAME)
    while results.has_next():
        title, path, position, heading, text, sim, context_chunks, linked_notes = results.get_next()
        
        # Format linked notes into a lightweight context line
        linked_str = f"Related Notes: {', '.join(linked_notes)}" if linked_notes else "Related Notes: None"

        # Format path into a valid obsidian uri
        obsidian_uri = f"obsidian://open?vault={encoded_vault}&file={quote(path)}"
        
        context_str = ""
        
        # Format context cunks if present
        if context_chunks:
            chunk_texts = [f" - {c['heading']}: {c['text']}" for c in context_chunks if c['text']]
            context_str = "\nSurrounding Context:\n" + "\n".join(chunk_texts)

        # Indexed context block for clean citations
        block = (
            f"[{doc_index}] Document Title: {title}\n"
            f"Obsidian URI: {obsidian_uri}\n"
            f"Section: {heading}\n"
            f"{linked_str}\n"
            f"Content: {text}{context_str}"
        )
        context_blocks.append(block)
        doc_index += 1
            
    logging.info(f"context:----\n{context_blocks}\n----")

    # Short-circuit if no relevant chunks met threshold
    if not context_blocks:
        async def empty_response_generator():
            yield "I couldn't find any relevant information in your Obsidian vault to answer that question."
        return StreamingResponse(empty_response_generator(), media_type="text/event-stream")

    context_text = "\n\n---\n\n".join(context_blocks)
    if len(context_text) > MAX_CONTEXT_CHARS:
        logging.warning(f"Context too long ({len(context_text)} chars). Truncating to {MAX_CONTEXT_CHARS}.")
        context_text = context_text[:MAX_CONTEXT_CHARS] + "\n\n...[CONTEXT TRUNCATED FOR LENGTH]..."

    # 1. Clean System Prompt
    system_prompt = (
        "You are a precise knowledge assistant. Answer the user's question using ONLY the provided context."
    )

    # 2. User Prompt (Context -> Rules -> Question -> Action Command)
    user_prompt = (
        f"--- CONTEXT ---\n{context_text}\n--- END CONTEXT ---\n\n"
        "### CRITICAL FORMATTING RULES:\n"
        "1. You MUST cite your sources inline at the end of sentences using brackets (e.g., [1]).\n"
        "2. You MUST include a '### Sources' list at the very end of your response.\n"
        "3. Format each source exactly like this: [1] [Document Title](obsidian://open?vault=...)\n\n"
        f"Question: {req.query}\n"
        "Now, answer the question using ONLY the provided context and follow the formatting rules."
    )

    # 3. Qwen ChatML Prompt Format
    prompt = (
        f"<|im_start|>system\n{system_prompt}<|im_end|>\n"
        f"<|im_start|>user\n{user_prompt}<|im_end|>\n"
        "<|im_start|>assistant\n"
    )
    
    def stream_generator():
        stream = llm(
            prompt,
            max_tokens=MAX_TOKENS,
            temperature=TEMPERATURE,
            stream=True,
            stop=["<|im_end|>", "<|im_start|>"]
        )
        for chunk in stream:
            text = chunk["choices"][0]["text"]
            if text:
                yield text

    return StreamingResponse(stream_generator(), media_type="text/event-stream")

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="127.0.0.1", port=8000)
