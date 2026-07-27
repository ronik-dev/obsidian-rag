import os
import re
import hashlib
import logging
import kuzu
import json
from sentence_transformers import SentenceTransformer

logging.basicConfig(level=logging.INFO)

# Configuration
VAULT_PATH = os.path.expanduser(os.getenv('VAULT_PATH', '~/.obsidian_vault/LocalArchVault'))
DB_PATH = os.path.expanduser(os.getenv('DB_PATH', '../data/vault.db'))

# Regex for Obsidian wikilinks: matches [[Link]] or [[Link|Alias]]
WIKILINK_RE = re.compile(r'\[\[(.*?)(?:\|.*?)?\]\]')

def chunk_markdown(text, max_chars=2000):
    """Splits markdown by headings, forcing a split if a chunk exceeds max_chars."""
    # Clean up non-breaking spaces from pasted code/text
    text = text.replace('\xa0', '')
    
    lines = text.split('\n')
    chunks = []
    current_heading = "Top Level"
    current_text = []
    current_length = 0

    for line in lines:
        # Split if we hit a heading
        if line.startswith(('# ', '## ', '### ')):
            if current_text and "".join(current_text).strip():
                chunks.append((current_heading, "\n".join(current_text)))
            current_heading = line.lstrip('#').strip()
            current_text = [line]
            current_length = len(line)
        else:
            # Split if adding this line makes the chunk too massive
            if current_length + len(line) > max_chars and current_text:
                chunks.append((current_heading, "\n".join(current_text)))
                # Mark the next chunk as a continuation
                current_heading = f"{current_heading} (Cont.)"
                current_text = [line]
                current_length = len(line)
            else:
                current_text.append(line)
                current_length += len(line) + 1
            
    # Append the final chunk
    if current_text and "".join(current_text).strip():
        chunks.append((current_heading, "\n".join(current_text)))
        
    return chunks

def chunk_canvas(file_path):
    """Parses an Obsidian Canvas JSON file into logical chunks."""
    chunks = []
    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            data = json.load(f)
            
        nodes = data.get("nodes", [])
        for position, node in enumerate(nodes, start=1):
            node_type = node.get("type")
            
            # Text nodes (standard markdown cards in the canvas)
            if node_type == "text":
                text = node.get("text", "").strip()
                if text:
                    # Treat the card text as markdown to chunk it properly!
                    sub_chunks = chunk_markdown(text)
                    for heading, sub_text in sub_chunks:
                        chunks.append((f"Canvas Text Node: {heading}", sub_text))
                    
            # File references (embedded images, pdfs, or other notes)
            elif node_type == "file":
                file_ref = node.get("file", "")
                if file_ref:
                    chunks.append((f"Canvas File Reference", f"Links to file: {file_ref}"))
                    
            # Web links
            elif node_type == "link":
                url = node.get("url", "")
                if url:
                    chunks.append((f"Canvas URL", f"External Link: {url}"))
                    
    except json.JSONDecodeError:
        logging.error(f"Failed to parse Canvas file: {file_path}")
        
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


        # Process Markdown and Canvas files
        for file in files:
            is_md = file.endswith('.md')
            is_canvas = file.endswith('.canvas')
            
            if not (is_md or is_canvas):
                continue
                
            file_path = os.path.join(root, file)
            rel_path = os.path.relpath(file_path, VAULT_PATH)
            title = os.path.splitext(file)[0]
            doc_type = 'markdown' if is_md else 'canvas'

            active_doc_paths.add(rel_path)
            
            # Create Document Node
            conn.execute(
                "MERGE (d:Document {path: $path}) ON CREATE SET d.title = $title, d.type = $type",
                {"path": rel_path, "title": title, "type": doc_type}
            )
            
            # Link Document to parent Folder
            conn.execute(
                "MATCH (f:Folder {path: $f_path}), (d:Document {path: $d_path}) "
                "MERGE (f)-[:CONTAINS]->(d)",
                {"f_path": folder_path, "d_path": rel_path}
            )
            
            # Route to the correct parser
            chunks = []
            if is_md:
                with open(file_path, 'r', encoding='utf-8') as f:
                    content = f.read()
                
                # Extract wikilinks for Pass 2
                for link in WIKILINK_RE.findall(content):
                    links_to_process.append((rel_path, link.strip()))
                    
                chunks = chunk_markdown(content)
                
            elif is_canvas:
                chunks = chunk_canvas(file_path)
                
                # Also extract wikilinks from Canvas text nodes!
                for heading, text in chunks:
                    for link in WIKILINK_RE.findall(text):
                        links_to_process.append((rel_path, link.strip()))

            
            for position, (heading, text) in enumerate(chunks, start=1):
                unique_string = f"{rel_path}::{position}::{text}"
                chunk_id = hashlib.sha256(unique_string.encode("utf-8")).hexdigest()
            
                active_chunk_ids.add(chunk_id)
            
                # Skip embedding if this exact chunk already exists
                result = conn.execute(
                    "MATCH (c:Chunk {id: $id}) RETURN c.id",
                    {"id": chunk_id},
                )
            
                if not result.has_next():
                    embedding = embedder.encode(text).tolist()
            
                    conn.execute(
                        """
                        CREATE (c:Chunk {
                            id: $id,
                            position: $position,
                            text: $text,
                            heading: $heading,
                            embedding: $embedding
                        })
                        """,
                        {
                            "id": chunk_id,
                            "position": position,
                            "text": text,
                            "heading": heading,
                            "embedding": embedding,
                        },
                    )
            
                # Ensure the PART_OF relationship exists
                conn.execute(
                    """
                    MATCH (c:Chunk {id: $c_id}), (d:Document {path: $d_path})
                    MERGE (c)-[:PART_OF]->(d)
                    """,
                    {
                        "c_id": chunk_id,
                        "d_path": rel_path,
                    },
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

    logging.info("Pass 2 complete. Wired up wikilinks")
    logging.info("Ingestion complete!")

if __name__ == "__main__":
    ingest_vault()
