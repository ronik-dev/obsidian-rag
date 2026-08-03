#include "doctest.h"
#include "../src/graph/graph.h"

TEST_SUITE("VaultGraph Core Functionality") {

    TEST_CASE("Adding a Document") {
        VaultGraph graph;

        DocumentNode doc;
        doc.path = "Linux_Notes.md";
        doc.title = "Linux Notes";
        doc.tags = {"#Dev/Linux"};

        graph.add_document(doc);

        REQUIRE(graph.documents.size() == 1);
        CHECK(graph.documents["Linux_Notes.md"].title == "Linux Notes");
        CHECK(graph.documents["Linux_Notes.md"].tags[0] == "#Dev/Linux");
    }

    TEST_CASE("Adding a Chunk automatically wires it to the parent Document") {
        VaultGraph graph;

        // 1. Add Parent Doc
        DocumentNode doc;
        doc.path = "Architecture.md";
        graph.add_document(doc);

        // 2. Add Chunk
        ChunkNode chunk;
        chunk.id = "chunk_123";
        chunk.doc_path = "Architecture.md";
        chunk.position = 0;
        chunk.text = "System overview.";

        graph.add_chunk(chunk);

        REQUIRE(graph.chunks.size() == 1);
        CHECK(graph.chunks["chunk_123"].text == "System overview.");

        // 3. Verify the wiring: the document should now know about this chunk
        REQUIRE(graph.documents["Architecture.md"].chunk_ids.size() == 1);
        CHECK(graph.documents["Architecture.md"].chunk_ids[0] == "chunk_123");
    }

    TEST_CASE("Context Expansion: Fetching surrounding chunks (Budgeted)") {
        VaultGraph graph;

        DocumentNode doc{.path = "LongDoc.md"};
        graph.add_document(doc);

        // Add 5 consecutive chunks. Text length is exactly 6 chars ("Text X").
        for (size_t i = 0; i < 5; ++i) {
            ChunkNode c;
            c.id = "c_" + std::to_string(i);
            c.doc_path = "LongDoc.md";
            c.position = i;
            c.text = "Text " + std::to_string(i); // length = 6
            graph.add_chunk(c);
        }

        // Scenario 1: Hitting the middle chunk ("c_2").
        // We set a budget of 20 chars. This safely fits 3 chunks (3 * 6 = 18 chars).
        auto context = graph.get_budgeted_context("c_2", 20);

        REQUIRE(context.size() == 3);
        // The BFS prioritizes: Seed, then Seed+1, then Seed-1
        CHECK(context[0].id == "c_2");
        CHECK(context[1].id == "c_3"); 
        CHECK(context[2].id == "c_1");

        // Scenario 2: Hitting the first chunk ("c_0").
        // The BFS knows it's at the boundary and only expands to the right.
        auto edge_context = graph.get_budgeted_context("c_0", 20);

        REQUIRE(edge_context.size() == 3);
        CHECK(edge_context[0].id == "c_0");
        CHECK(edge_context[1].id == "c_1");
        CHECK(edge_context[2].id == "c_2");
    }
}

TEST_SUITE("VaultGraph Advanced Features") {

    TEST_CASE("Dynamic Updates: Deleting a document cascades to its chunks") {
        VaultGraph graph;

        DocumentNode doc{.path = "Stale.md", .title = "Stale"};
        graph.add_document(doc);

        ChunkNode c1{.id = "c1", .doc_path = "Stale.md", .text = "Part 1"};
        ChunkNode c2{.id = "c2", .doc_path = "Stale.md", .text = "Part 2"};
        graph.add_chunk(c1);
        graph.add_chunk(c2);

        REQUIRE(graph.chunks.size() == 2);

        // Action: Remove the document
        graph.remove_document("Stale.md");

        // Verification: Document and its orphaned chunks must be gone
        CHECK(graph.documents.find("Stale.md") == graph.documents.end());
        CHECK(graph.chunks.find("c1") == graph.chunks.end());
        CHECK(graph.chunks.find("c2") == graph.chunks.end());
    }

    TEST_CASE("Link Resolution: Chunk-to-Chunk with Header Fallbacks") {
        VaultGraph graph;

        DocumentNode doc{.path = "Linux.md", .title = "Linux"};
        graph.add_document(doc);

        // Chunk 1: The target of a standard header link
        ChunkNode c1{.id = "c_intro", .doc_path = "Linux.md", .header = "Intro", .text = "Linux is great."};
        // Chunk 2: The target of a deep link (H5) that wasn't split by the parser
        ChunkNode c2{.id = "c_perms", .doc_path = "Linux.md", .header = "Security", .text = "Some text.\n##### Permissions\nChmod 777 is bad."};
        // Chunk 3: The source chunk making the links
        ChunkNode c3{.id = "c_source", .doc_path = "Linux.md", .header = "Links", .text = "See intro and perms."};

        graph.add_chunk(c1);
        graph.add_chunk(c2);
        graph.add_chunk(c3);

        // 1. Resolve exact header match
        bool exact_success = graph.resolve_and_add_link("c_source", "Linux.md", "Intro");
        CHECK(exact_success == true);

        // 2. Resolve fallback substring match (the H5 "Permissions")
        bool fallback_success = graph.resolve_and_add_link("c_source", "Linux.md", "Permissions");
        CHECK(fallback_success == true);

        // Verify the source chunk now holds the correct target IDs
        REQUIRE(graph.chunks["c_source"].links_to.size() == 2);
        CHECK(graph.chunks["c_source"].links_to.count("c_intro") == 1);
        CHECK(graph.chunks["c_source"].links_to.count("c_perms") == 1);
    }

    TEST_CASE("Budgeted BFS: Strict Character Limits") {
        VaultGraph graph;

        DocumentNode doc{.path = "Graph.md"};
        graph.add_document(doc);

        // Seed chunk (Length: 50)
        graph.add_chunk({.id = "seed", .doc_path = "Graph.md", .position = 1, .text = std::string(50, 'A')});
        // Spatial neighbor (Length: 50)
        graph.add_chunk({.id = "spatial", .doc_path = "Graph.md", .position = 2, .text = std::string(50, 'B')});
        // Linked neighbor (Length: 50)
        graph.add_chunk({.id = "linked", .doc_path = "Graph.md", .position = 9, .text = std::string(50, 'C')});

        // Wire the link
        graph.chunks["seed"].links_to.insert("linked");

        // Scenario 1: Budget of 120 chars. 
        // Should fit Seed (50) + Spatial (50) = 100. Linked (50) would push to 150, so it's skipped.
        auto context_small = graph.get_budgeted_context("seed", 120);
        REQUIRE(context_small.size() == 2);
        CHECK(context_small[0].id == "seed");
        CHECK(context_small[1].id == "spatial"); // Prioritizes spatial over links

        // Scenario 2: Budget of 200 chars.
        // Should fit all three.
        auto context_large = graph.get_budgeted_context("seed", 200);
        REQUIRE(context_large.size() == 3);
    }
}
