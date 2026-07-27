import kuzu
import os

DB_PATH = os.getenv('DB_PATH', '../data/vault.db')

def check():
    db = kuzu.Database(DB_PATH, read_only=True)
    conn = kuzu.Connection(db)
    
    print("--- Database Counts ---")
    print("Documents: ", conn.execute("MATCH (d:Document) RETURN count(d)").get_next()[0])
    print("Chunks:    ", conn.execute("MATCH (c:Chunk) RETURN count(c)").get_next()[0])
    print("PART_OF:   ", conn.execute("MATCH ()-[e:PART_OF]->() RETURN count(e)").get_next()[0])

if __name__ == "__main__":
    check()
