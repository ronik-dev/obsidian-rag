import logging
from contextlib import asynccontextmanager
from fastapi import FastAPI
from fastapi.responses import StreamingResponse
from pydantic import BaseModel
import kuzu
import os
from sentence_transformers import SentenceTransformer
from llama_cpp import Llama

logging.basicConfig(level=logging.INFO)

# Global State
db = None
conn = None
llm = None
embedder = None

# Configuration -- llamacpp
MODEL_PATH = os.getenv('MODEL_PATH', 'models/qwen2.5-1.5b-instruct-q4_k_m.gguf')
N_CTX = int(os.getenv('N_CTX', '4096'))                                     # Context window size                                   
N_THREADS = int(os.getenv('N_THREADS', '4'))                                # Adjust based on your Alder Lake CPU (e.g., 4 P-cores)
VERBOSE = os.getenv('VERBOSE', 'False').lower() in ('true', '1', 't')       # Keeps terminal output clean 

# Configuration -- custom
# Fixed the typo from TRASHOLD to THRESHOLD and cast to float
SIMILARITY_THRESHOLD = float(os.getenv('SIMILARITY_THRESHOLD', '0.3'))      # (0.3 is a safe baseline for all-MiniLM-L6-v2)
MAX_TOKENS = int(os.getenv('MAX_TOKENS', '1024'))

@asynccontextmanager
async def lifespan(app: FastAPI):
    global db, conn, llm, embedder
    
    logging.info("Loading KùzuDB...")
    db = kuzu.Database("../data/vault.db")
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

@app.post("/query")
async def handle_query(req: QueryRequest):
    # 1. Embed the incoming user query
    query_embedding = embedder.encode(req.query).tolist()

    # 2. Search KuzuDB (Hybrid approach)
    # We traverse the graph to get the parent Document's title, 
    # while calculating the cosine similarity of the Chunk's embedding.
    cypher_query = """
    MATCH (c:Chunk)-[:PART_OF]->(d:Document)
    WITH c, d, array_cosine_similarity(c.embedding, $q_embed) AS sim
    ORDER BY sim DESC
    LIMIT 5
    RETURN d.title, c.heading, c.text, sim
    """
    
    results = conn.execute(cypher_query, {"q_embed": query_embedding})
    
    # 3. Format the retrieved context
    context_blocks = []

    while results.has_next():
        title, heading, text, sim = results.get_next()
        # Only inject chunks that are somewhat relevant 
        if sim > SIMILARITY_THRESHOLD:
            context_blocks.append(f"Source Document: {title}\nSection: {heading}\nContent: {text}")
            
    # SHORT-CIRCUIT: If no context is found, do not ask Qwen.
    if not context_blocks:
        async def empty_response_generator():
            yield "I couldn't find any relevant information in your Obsidian vault to answer that question."
        return StreamingResponse(empty_response_generator(), media_type="text/event-stream")

    context_text = "\n\n".join(context_blocks)

    # 4. Construct the Augmented Prompt
    system_prompt = (
        "You are a highly precise local knowledge assistant. Answer the user's question "
        "using ONLY the provided context. If the answer cannot be found in the context, "
        "say so directly without guessing.\n\n"
        f"--- CONTEXT ---\n{context_text}\n--- END CONTEXT ---"
    )
    
    # Qwen ChatML formatting
    prompt = (
        f"<|im_start|>system\n{system_prompt}<|im_end|>\n"
        f"<|im_start|>user\n{req.query}<|im_end|>\n"
        "<|im_start|>assistant\n"
    )
    
    # 5. Stream the generation back to the client
    def stream_generator():
        stream = llm(
            prompt,
            max_tokens=MAX_TOKENS,
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
