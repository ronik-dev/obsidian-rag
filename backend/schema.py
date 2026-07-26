import kuzu
import os
import logging

logging.basicConfig(level=logging.INFO)
DB_PATH = os.getenv('DB_PATH', '../data/vault.db')

def init_schema():
    """Initialize Kùzu db schema."""

    os.makedirs(os.path.dirname(DB_PATH), exist_ok=True)

    logging.info(f"Connecting to KùzuDB at {DB_PATH}")
    db = kuzu.Database(DB_PATH)
    conn = kuzu.Connection(db)
    
    # Node tables
    node_queries = [
        # Folder: Represents physical directories
        "CREATE NODE TABLE Folder (name STRING, path STRING, PRIMARY KEY (path))",
        
        # Document: Represents .md, .pdf, or .canvas files
        "CREATE NODE TABLE Document (title STRING, path STRING, type STRING, PRIMARY KEY (path))",
        
        # Chunk: Represents a section of text. 
        # The embedding uses FLOAT[384] to match all-MiniLM-L6-v2's output dimension.
        "CREATE NODE TABLE Chunk (id STRING, text STRING, heading STRING, embedding FLOAT[384], PRIMARY KEY (id))" 
    ]

    # Relationship (Edge) Tables
    edge_queries = [
        # Hierarchy: Folders can contain other Folders or Documents
        "CREATE REL TABLE CONTAINS (FROM Folder TO Folder, FROM Folder TO Document)",
        
        # Composition: Chunks belong to a parent Document
        "CREATE REL TABLE PART_OF (FROM Chunk TO Document)",
        
        # Obsidian Wikilinks: Documents linking to other Documents
        "CREATE REL TABLE LINKS_TO (FROM Document TO Document)"
    ]

    # Execute Node Creation
    logging.info("Creating node tables...")
    for query in node_queries:
        try:
            conn.execute(query)
            logging.info(f"Success: {query.split('(')[0].strip()}")
        except RuntimeError as e:
            # Kuzu throws an error if the table already exists
            logging.warning(f"Skipped: {e}")

    # Execute Edge Creation
    logging.info("Creating Relationship Tables...")
    for query in edge_queries:
        try:
            conn.execute(query)
            logging.info(f"Success: {query.split('(')[0].strip()}")
        except RuntimeError as e:
            logging.warning(f"Skipped: {e}")

    logging.info("Schema initialization complete!")

if __name__ == "__main__":
    init_schema()
