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
    EXPECT_EQ(input.find(IOpenAIChat::EMBEDDING_TAG), std::string::npos);
}

// ===========================================================================
// checkForMaliciousPayloads – Tag at start
// ===========================================================================
TEST(MaliciousPayloadsTest, TagAtStart) {
    std::string input = std::string(IOpenAIChat::EMBEDDING_TAG) + "something";
    llmui::checkForMaliciousPayloads(input);
    EXPECT_EQ(input.find(IOpenAIChat::EMBEDDING_TAG), std::string::npos);
}

// ===========================================================================
// checkForMaliciousPayloads – Tag at end
// ===========================================================================
TEST(MaliciousPayloadsTest, TagAtEnd) {
    std::string input = "something" + std::string(IOpenAIChat::EMBEDDING_TAG);
    llmui::checkForMaliciousPayloads(input);
    EXPECT_EQ(input.find(IOpenAIChat::EMBEDDING_TAG), std::string::npos);
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
    EXPECT_EQ(input.find(IOpenAIChat::EMBEDDING_TAG), std::string::npos);
}

// ===========================================================================
// checkForMaliciousPayloads – Fake media marker is replaced
// ===========================================================================
TEST(MaliciousPayloadsTest, FakeMediaMarker) {
    std::string input = "look at this [photo]";
    llmui::checkForMaliciousPayloads(input);
    EXPECT_EQ(input.find("[photo]"), std::string::npos);
}

// ===========================================================================
// checkForMaliciousPayloads – Single bracket is fine
// ===========================================================================
TEST(MaliciousPayloadsTest, SingleBracketUnchanged) {
    std::string input = "1) first, 2] second";
    std::string original = input;
    llmui::checkForMaliciousPayloads(input);
    EXPECT_EQ(input, original);
}
