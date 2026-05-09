#include "Diary.h"
#include <gmock/gmock.h>

// =====================================================================
// Diary::parse
// =====================================================================

TEST(DiaryParseUnit, ParseWithoutMetadata) {
    auto entries = AVector<Diary::Entry>{
        { .id = "entry1", .text = "Just a regular diary entry without metadata." }
    };
    auto result = Diary::parse(entries);

    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result.front().id, "entry1");
    EXPECT_EQ(result.front().freeformBody, "Just a regular diary entry without metadata.");
    EXPECT_EQ(result.front().metadata.usageCount, 0);
    EXPECT_EQ(result.front().metadata.lastUsed, "never");
}

TEST(DiaryParseUnit, ParseWithMetadata) {
    auto entries = AVector<Diary::Entry>{
        { .id = "test1", .text = "---\n{\"score\": 0.0, \"lastUsed\": \"2026-01-01\", \"usageCount\": 5, \"embedding\": []}\n---\nToday was a great day!" }
    };
    auto result = Diary::parse(entries);

    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result.front().id, "test1");
    EXPECT_EQ(result.front().freeformBody, "Today was a great day!");
    EXPECT_EQ(result.front().metadata.usageCount, 5);
    EXPECT_EQ(result.front().metadata.lastUsed, "2026-01-01");
}

TEST(DiaryParseUnit, ParseCorruptedMetadata) {
    auto entries = AVector<Diary::Entry>{
        { .id = "bad", .text = "---\n{invalid json\n---\nBody text here" }
    };
    auto result = Diary::parse(entries);

    // Should gracefully handle corrupted metadata and treat entire text as body
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result.front().id, "bad");
    EXPECT_EQ(result.front().freeformBody, "---\n{invalid json\n---\nBody text here");
    EXPECT_EQ(result.front().metadata.usageCount, 0);
}

TEST(DiaryParseUnit, ParseMultipleEntries) {
    auto entries = AVector<Diary::Entry>{
        { .id = "e1", .text = "---\n{\"score\": 0.0, \"lastUsed\": \"never\", \"usageCount\": 3, \"embedding\": []}\n---\nFirst entry" },
        { .id = "e2", .text = "Second entry without metadata" },
        { .id = "e3", .text = "---\n{\"score\": 0.0, \"lastUsed\": \"never\", \"usageCount\": 10, \"embedding\": []}\n---\nThird entry" },
    };
    auto result = Diary::parse(entries);

    ASSERT_EQ(result.size(), 3);

    auto it = result.begin();
    EXPECT_EQ(it->id, "e1");
    EXPECT_EQ(it->freeformBody, "First entry");
    EXPECT_EQ(it->metadata.usageCount, 3);
    ++it;

    EXPECT_EQ(it->id, "e2");
    EXPECT_EQ(it->freeformBody, "Second entry without metadata");
    EXPECT_EQ(it->metadata.usageCount, 0);
    ++it;

    EXPECT_EQ(it->id, "e3");
    EXPECT_EQ(it->freeformBody, "Third entry");
    EXPECT_EQ(it->metadata.usageCount, 10);
}

TEST(DiaryParseUnit, ParseMetadataOnlyNoBody) {
    auto entries = AVector<Diary::Entry>{
        { .id = "metaonly", .text = "---\n{\"score\": 0.0, \"lastUsed\": \"never\", \"usageCount\": 1, \"embedding\": []}\n---\n " }
    };
    auto result = Diary::parse(entries);

    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result.front().id, "metaonly");
    EXPECT_EQ(result.front().freeformBody, " ");
    EXPECT_EQ(result.front().metadata.usageCount, 1);
}

TEST(DiaryParseUnit, ParseEmptyInput) {
    auto entries = AVector<Diary::Entry>{
        { .id = "empty", .text = "" }
    };
    auto result = Diary::parse(entries);

    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result.front().id, "empty");
    EXPECT_TRUE(result.front().freeformBody.empty());
    EXPECT_EQ(result.front().metadata.usageCount, 0);
}

TEST(DiaryParseUnit, ParseWithScoreAndConfidence) {
    auto entries = AVector<Diary::Entry>{
        { .id = "scored", .text = "---\n{\"score\": 0.85, \"confidence\": 0.5, \"lastUsed\": \"never\", \"usageCount\": 2, \"embedding\": []}\n---\nImportant memory" }
    };
    auto result = Diary::parse(entries);

    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result.front().freeformBody, "Important memory");
    EXPECT_FLOAT_EQ(result.front().metadata.score, 0.85f);
    EXPECT_FLOAT_EQ(result.front().metadata.confidence, 0.5f);
    EXPECT_EQ(result.front().metadata.usageCount, 2);
}

TEST(DiaryParseUnit, ParseWithEmbedding) {
    auto entries = AVector<Diary::Entry>{
        { .id = "emb", .text = "---\n{\"score\": 0.0, \"lastUsed\": \"never\", \"usageCount\": 0, \"embedding\": [0.1, 0.2, 0.3]}\n---\nHas embedding" }
    };
    auto result = Diary::parse(entries);

    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result.front().freeformBody, "Has embedding");
    ASSERT_EQ(result.front().metadata.embedding.size(), 3);
    EXPECT_DOUBLE_EQ(result.front().metadata.embedding[0], 0.1);
    EXPECT_DOUBLE_EQ(result.front().metadata.embedding[1], 0.2);
    EXPECT_DOUBLE_EQ(result.front().metadata.embedding[2], 0.3);
}

TEST(DiaryParseUnit, ParseTrimsNewlines) {
    auto entries = AVector<Diary::Entry>{
        { .id = "trim", .text = "\n\n---\n{\"score\": 0.0, \"lastUsed\": \"never\", \"usageCount\": 1, \"embedding\": []}\n---\n\nContent\n\n" }
    };
    auto result = Diary::parse(entries);

    ASSERT_EQ(result.size(), 1);
    // Diary::parse trims '\n' from entry.text before processing.
    // The body after metadata may have leading/trailing whitespace trimmed.
    EXPECT_EQ(std::string(result.front().freeformBody), "\nContent");
    EXPECT_EQ(result.front().metadata.usageCount, 1);
}
