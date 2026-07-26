import os
import re
import hashlib
import logging
import kuzu
from sentence_transformers import SentenceTransformer

logging.basicConfig(level=logging.INFO)

# Configuration
VAULT_PATH = os.path.expanduser(os.getenv('VAULT_PATH', '~/.obsidian_vault/LocalArchVault'))
DB_PATH = os.path.expanduser(os.getenv('DB_PATH', '../data/vault.db'))

# Regex for Obsidian wikilinks: matches [[Link]] or [[Link|Alias]]
WIKILINK_RE = re.compile(r'\[\[(.*?)(?:\|.*?)?\]\]')

def chunk_markdown(text):
    """Splits markdown by headings (H1, H2, H3) into logical chunks."""
    lines = text.split('\n')
    chunks = []
    current_heading = "Top Level"
    current_text = []

    for line in lines:
        if line.startswith(('# ', '## ', '### ')):
            # Save the previous chunk if it has content
            if current_text and "".join(current_text).strip():
                chunks.append((current_heading, "\n".join(current_text)))
            current_heading = line.lstrip('#').strip()
            current_text = [line]
        else:
            current_text.append(line)
            
    # Append the final chunk
    if current_text and "".join(current_text).strip():
        chunks.append((current_heading, "\n".join(current_text)))
        
    return chunks

def ingest_vault():
    logging.info("Loading embedding model (all-MiniLM-L6-v2)...")
    embedder = SentenceTransformer('all-MiniLM-L6-v2')
    
    db = kuzu.Database(DB_PATH)
    conn = kuzu.Connection(db)
    
    links_to_process = [] # Store tuples of (source_path, target_title) for Pass 2
    
    # Sets to track what actually exists on disk during this run
    active_doc_paths = set()
    active_chunk_ids = set()

    logging.info(f"Scanning vault at {VAULT_PATH}...")
    
    # PASS 1: Nodes and Hierarchy
    for root, dirs, files in os.walk(VAULT_PATH):
        # Skip hidden directories like .obsidian or .git
        dirs[:] = [d for d in dirs if not d.startswith('.')]
        
        folder_path = os.path.relpath(root, VAULT_PATH)
        if folder_path == ".":
            folder_path = "root"
            
        # Ensure current folder exists
        conn.execute("MERGE (f:Folder {path: $path}) ON CREATE SET f.name = $name", 
                     {"path": folder_path, "name": os.path.basename(root) or "root"})

        # Process Markdown files
        for file in files:
            if not file.endswith('.md'):
                continue
                
            file_path = os.path.join(root, file)
            rel_path = os.path.relpath(file_path, VAULT_PATH)
            title = os.path.splitext(file)[0]

            active_doc_paths.add(rel_path)
            
            # Create Document Node
            conn.execute(
                "MERGE (d:Document {path: $path}) ON CREATE SET d.title = $title, d.type = 'markdown'",
                {"path": rel_path, "title": title}
            )
            
            # Link Document to parent Folder
            conn.execute(
                "MATCH (f:Folder {path: $f_path}), (d:Document {path: $d_path}) "
                "MERGE (f)-[:CONTAINS]->(d)",
                {"f_path": folder_path, "d_path": rel_path}
            )
            
            # Read, parse, embed, and insert Chunks
            with open(file_path, 'r', encoding='utf-8') as f:
                content = f.read()
                
            # Extract wikilinks for Pass 2
            for link in WIKILINK_RE.findall(content):
                links_to_process.append((rel_path, link.strip()))

            chunks = chunk_markdown(content)
            for index, (heading, text) in enumerate(chunks):
                unique_string = f"{rel_path}::{index}::{text}"
                chunk_id = hashlib.sha256(unique_string.encode('utf-8')).hexdigest()
                embedding = embedder.encode(text).tolist()

                active_chunk_ids.add(chunk_id)

                result = conn.execute("MATCH (c:Chunk {id: $id}) RETURN c.id", {"id": chunk_id})
                if result.has_next():
                    continue # Chunk already in DB, skip heavy embedding computation

                embedding = embedder.encode(text).tolist()

                conn.execute(
                    """
                    MERGE (c:Chunk {id: $id})
                    ON CREATE SET c.text = $text, c.heading = $heading, c.embedding = $embedding
                    """,
                    {"id": chunk_id, "text": text, "heading": heading, "embedding": embedding}
                )
                
                # Link Chunk to Document
                conn.execute(
                    "MATCH (c:Chunk {id: $c_id}), (d:Document {path: $d_path}) "
                    "MERGE (c)-[:PART_OF]->(d)",
                    {"c_id": chunk_id, "d_path": rel_path}
                )

    logging.info("Pass 1 complete. Pruning deleted/modified data...")

    # PRUNE STALE CHUNKS 
    # Get all chunk IDs currently in the database
    db_chunks = []
    result = conn.execute("MATCH (c:Chunk) RETURN c.id")
    while result.has_next():
        db_chunks.append(result.get_next()[0])
        
    # Find chunks in DB that are no longer on disk (or were modified)
    stale_chunks = set(db_chunks) - active_chunk_ids
    if stale_chunks:
        logging.info(f"Deleting {len(stale_chunks)} stale chunks...")
        for cid in stale_chunks:
            # Delete the PART_OF edge first, then the Chunk node
            conn.execute("MATCH (c:Chunk {id: $id})-[e:PART_OF]->() DELETE e", {"id": cid})
            conn.execute("MATCH (c:Chunk {id: $id}) DELETE c", {"id": cid})

    # PRUNE STALE DOCUMENTS 
    # Get all document paths currently in the database
    db_docs = []
    result = conn.execute("MATCH (d:Document) RETURN d.path")
    while result.has_next():
        db_docs.append(result.get_next()[0])
        
    # Find documents in DB that were deleted from disk
    stale_docs = set(db_docs) - active_doc_paths
    if stale_docs:
        logging.info(f"Deleting {len(stale_docs)} stale documents...")
        for path in stale_docs:
            # Delete any edges connected to the document (CONTAINS, LINKS_TO, PART_OF)
            conn.execute("MATCH (d:Document {path: $path})-[e]-() DELETE e", {"path": path})
            conn.execute("MATCH (d:Document {path: $path}) DELETE d", {"path": path})
            
    logging.info("Pruning complete. Wiring up wikilinks...")
    

    # PASS 2: Obsidian Wikilinks
    for source_path, target_title in links_to_process:
        # We match the target by title since wikilinks usually omit the full path
        conn.execute(
            "MATCH (src:Document {path: $src_path}), (tgt:Document {title: $tgt_title}) "
            "MERGE (src)-[:LINKS_TO]->(tgt)",
            {"src_path": source_path, "tgt_title": target_title}
        )

    logging.info("Ingestion complete!")

if __name__ == "__main__":
    ingest_vault()
