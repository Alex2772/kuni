//
// Created by alex2772 on 5/9/26.
//

#include "llmui/malicious_payloads.h"
#include "IOpenAIChat.h"

#include <gmock/gmock.h>

using namespace testing;

// ===========================================================================
// checkForMaliciousPayloads – Contains EMBEDDING_TAG → replaced with "malicious"
// ===========================================================================
TEST(MaliciousPayloadsTest, ContainsEmbeddingTag) {
    std::string input = "Hello " + std::string(IOpenAIChat::EMBEDDING_TAG) + " world";
    llmui::checkForMaliciousPayloads(input);
    EXPECT_EQ(input, "malicious");
}

// ===========================================================================
// checkForMaliciousPayloads – Tag at start
// ===========================================================================
TEST(MaliciousPayloadsTest, TagAtStart) {
    std::string input = std::string(IOpenAIChat::EMBEDDING_TAG) + "something";
    llmui::checkForMaliciousPayloads(input);
    EXPECT_EQ(input, "malicious");
}

// ===========================================================================
// checkForMaliciousPayloads – Tag at end
// ===========================================================================
TEST(MaliciousPayloadsTest, TagAtEnd) {
    std::string input = "something" + std::string(IOpenAIChat::EMBEDDING_TAG);
    llmui::checkForMaliciousPayloads(input);
    EXPECT_EQ(input, "malicious");
}

// ===========================================================================
// checkForMaliciousPayloads – Clean string unchanged
// ===========================================================================
TEST(MaliciousPayloadsTest, CleanStringUnchanged) {
    std::string input = "Hello, this is a normal message!";
    std::string original = input;
    llmui::checkForMaliciousPayloads(input);
    EXPECT_EQ(input, original);
}

// ===========================================================================
// checkForMaliciousPayloads – Empty string unchanged
// ===========================================================================
TEST(MaliciousPayloadsTest, EmptyStringUnchanged) {
    std::string input;
    llmui::checkForMaliciousPayloads(input);
    EXPECT_TRUE(input.empty());
}

// ===========================================================================
// checkForMaliciousPayloads – Only tag
// ===========================================================================
TEST(MaliciousPayloadsTest, OnlyTag) {
    std::string input = IOpenAIChat::EMBEDDING_TAG;
    llmui::checkForMaliciousPayloads(input);
    EXPECT_EQ(input, "malicious");
}
