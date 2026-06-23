//
// Created by alex2772 on 6/6/26.
//

#include "tools/ask.h"
#include "../common.h"
#include "Diary.h"
#include "IOpenAIChat.h"
#include "OpenAITools.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AEventLoop.h"
#include "util/await_synchronously.h"

#include <gmock/gmock.h>

using namespace testing;

namespace {
// ---------------------------------------------------------------------------
// Mock IOpenAIChat
// ---------------------------------------------------------------------------
class OpenAIMock : public IOpenAIChat {
public:
    MOCK_METHOD((AArc<StreamingResponse>), chatStreaming, (Params params, IOpenAIChat::Session messages), (override));
    MOCK_METHOD(AFuture<std::valarray<double>>, embedding, (Params params, AString input), (override));
};

// ---------------------------------------------------------------------------
// Mock Diary
// ---------------------------------------------------------------------------
class DiaryMock : public Diary {
public:
    DiaryMock()
        : Diary(Init{
              .diaryDir = "/tmp/kuni_test_ask_diary",
              .openAI = nullptr,
          }) {}

    MOCK_METHOD(
        (AFuture<AVector<EntryExAndRelatedness>>),
        query,
        (const std::valarray<double>& query, QueryOpts opts),
        (override));
};

// ---------------------------------------------------------------------------
// Helper: wrap a Response into a StreamingResponse
// ---------------------------------------------------------------------------
static AArc<IOpenAIChat::StreamingResponse> makeStreamingResponse(IOpenAIChat::Response resp) {
    auto result = _new<IOpenAIChat::StreamingResponse>();
    result->response.raw = std::move(resp);
    result->completed.supplyValue();
    return result;
}

// ---------------------------------------------------------------------------
// Helper: build a Response with a single tool_call to #query
// ---------------------------------------------------------------------------
static AArc<IOpenAIChat::StreamingResponse> makeQueryToolCallResponse(const AString& text, const AString& callId = "call_1") {
    IOpenAIChat::Message msg;
    msg.role = IOpenAIChat::Message::Role::ASSISTANT;
    msg.content = "";
    msg.tool_calls = {
        IOpenAIChat::Message::ToolCall{
            .id = callId,
            .index = 0,
            .type = "function",
            .function = {
                .name = "query",
                .arguments = R"({"text": ")" + text + R"("})",
            },
        },
    };

    IOpenAIChat::Response resp;
    resp.choices = {
        IOpenAIChat::Response::Choice{
            .index = 0,
            .message = std::move(msg),
            .finish_reason = "tool_calls",
        },
    };
    return makeStreamingResponse(std::move(resp));
}

// ---------------------------------------------------------------------------
// Helper: build a final (no tool_call) Response
// ---------------------------------------------------------------------------
static AArc<IOpenAIChat::StreamingResponse> makeFinalResponse(const AString& content) {
    IOpenAIChat::Message msg;
    msg.role = IOpenAIChat::Message::Role::ASSISTANT;
    msg.content = content;

    IOpenAIChat::Response resp;
    resp.choices = {
        IOpenAIChat::Response::Choice{
            .index = 0,
            .message = std::move(msg),
            .finish_reason = "stop",
        },
    };
    return makeStreamingResponse(std::move(resp));
}

// ---------------------------------------------------------------------------
// Helper: empty valarray embedding returned from the mock
// ---------------------------------------------------------------------------
static std::valarray<double> dummyEmbedding() {
    return std::valarray<double>(0.0, 3);
}

} // namespace

// ===========================================================================
// ask – Handler: short query returns error, no LLM calls made
// ===========================================================================
TEST(AskTest, HandlerShortQueryError) {
    auto openAI = _new<OpenAIMock>();
    DiaryMock diary;
    auto tool = tools::ask([] { return AString{}; }, openAI, diary);

    EXPECT_CALL(*openAI, chatStreaming(testing::_, testing::_)).Times(0);
    EXPECT_CALL(*openAI, embedding(testing::_, testing::_)).Times(0);

    OpenAITools tools{};

    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"query", "short"}},
        .allToolCalls = {},
    }));

    EXPECT_TRUE(result.contains("too short query")) << "result = " << result;
    EXPECT_TRUE(result.contains("more context")) << "result = " << result;
}

// ===========================================================================
// ask – Handler: missing "query" arg throws
// ===========================================================================
TEST(AskTest, HandlerMissingQueryThrows) {
    auto openAI = _new<OpenAIMock>();
    DiaryMock diary;
    auto tool = tools::ask([] { return AString{}; }, openAI, diary);

    EXPECT_CALL(*openAI, chatStreaming(testing::_, testing::_)).Times(0);

    OpenAITools tools{};

    EXPECT_THROW(
        util::await_synchronously(tool.handler({
            .tools = tools,
            .args = AJson::Object{},
            .allToolCalls = {},
        })),
        AException
    );
}

// ===========================================================================
// ask – Handler: LLM makes one #query tool call then returns final answer
// ===========================================================================
TEST(AskTest, HandlerSuccessWithToolCall) {
    auto openAI = _new<OpenAIMock>();
    DiaryMock diary;
    auto tool = tools::ask([] { return AString{}; }, openAI, diary);

    const AString kFinalAnswer = "Alex writes ambient electronic music.";
    const AString kDiaryEntryBody = "Alex listens to ambient electronic music and writes his own tracks.";

    // embedding is called once by the internal #query tool handler
    EXPECT_CALL(*openAI, embedding(testing::_, testing::_))
        .WillOnce(Return(AFuture<std::valarray<double>>(dummyEmbedding())));

    // Build a list so we can return a valid iterator
    std::list<Diary::EntryEx> entryList;
    auto& entry = entryList.emplace_back();
    entry.id = "music_001";
    entry.freeformBody = kDiaryEntryBody;
    entry.metadata.score = 0.0f;
    entry.metadata.lastUsed = "2026-01-01";
    entry.metadata.usageCount = 0;
    entry.metadata.embedding = dummyEmbedding();

    Diary::EntryExAndRelatedness hit{entryList.begin(), 0.9};

    EXPECT_CALL(diary, query(testing::_, testing::_))
        .WillOnce(Return(AFuture<AVector<Diary::EntryExAndRelatedness>>(AVector<Diary::EntryExAndRelatedness>{hit})));

    // LLM: first call returns #query tool call, second returns final answer
    EXPECT_CALL(*openAI, chatStreaming(testing::_, testing::_))
        .WillOnce(Return(makeQueryToolCallResponse("What music does Alex write?")))
        .WillOnce(Return(makeFinalResponse(kFinalAnswer)));

    OpenAITools tools{};

    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"query", "What kind of music does Alex write?"}},
        .allToolCalls = {},
    }));

    EXPECT_TRUE(result.contains(kFinalAnswer)) << "result = " << result;
}

// ===========================================================================
// ask – Handler: LLM skips tool call on first response, gets reminded, then
//                makes a tool call and returns an answer
// ===========================================================================
TEST(AskTest, HandlerLLMForcedToCallTool) {
    auto openAI = _new<OpenAIMock>();
    DiaryMock diary;
    auto tool = tools::ask([] { return AString{}; }, openAI, diary);

    const AString kFinalAnswer = "Here is what I found.";

    // embedding called once after forced retry
    EXPECT_CALL(*openAI, embedding(testing::_, testing::_))
        .WillOnce(Return(AFuture<std::valarray<double>>(dummyEmbedding())));

    // diary returns no entries for simplicity
    EXPECT_CALL(diary, query(testing::_, testing::_))
        .WillOnce(Return(AFuture<AVector<Diary::EntryExAndRelatedness>>(AVector<Diary::EntryExAndRelatedness>{})));

    // 1st call: no tool_calls (LLM skips step) → gets "you must perform at least one call" message
    // 2nd call: makes the #query tool call
    // 3rd call: returns final answer
    EXPECT_CALL(*openAI, chatStreaming(testing::_, testing::_))
        .WillOnce(Return(makeFinalResponse("I think I know the answer already.")))
        .WillOnce(Return(makeQueryToolCallResponse("Alex music habits")))
        .WillOnce(Return(makeFinalResponse(kFinalAnswer)));

    OpenAITools tools{};

    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"query", "Tell me about Alex's music habits in detail."}},
        .allToolCalls = {},
    }));

    EXPECT_TRUE(result.contains(kFinalAnswer)) << "result = " << result;
}

// ===========================================================================
// ask – Handler: with temporaryContext, query is enriched
// ===========================================================================
TEST(AskTest, HandlerWithTemporaryContextEnrichesQuery) {
    auto openAI = _new<OpenAIMock>();
    DiaryMock diary;

    AString ctxContent = "User said they are learning guitar.";

    auto tool = tools::ask([&ctxContent] { return ctxContent; }, openAI, diary);

    const AString kFinalAnswer = "Found guitar-related diary entries.";

    EXPECT_CALL(*openAI, embedding(testing::_, testing::_))
        .WillOnce(Return(AFuture<std::valarray<double>>(dummyEmbedding())));

    EXPECT_CALL(diary, query(testing::_, testing::_))
        .WillOnce(Return(AFuture<AVector<Diary::EntryExAndRelatedness>>(AVector<Diary::EntryExAndRelatedness>{})));

    // Capture the messages passed to chat to verify query enrichment
    AString capturedUserContent;
    EXPECT_CALL(*openAI, chatStreaming(testing::_, testing::_))
        .WillOnce([&](IOpenAIChat::Params, IOpenAIChat::Session messages) {
            // First USER message should contain the enriched query with temporaryContext
            for (const auto& m : messages) {
                if (m.role == IOpenAIChat::Message::Role::USER) {
                    capturedUserContent = m.content;
                    break;
                }
            }
            return makeQueryToolCallResponse("guitar habits");
        })
        .WillOnce(Return(makeFinalResponse(kFinalAnswer)));

    OpenAITools tools{};

    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"query", "Does Alex play any musical instruments?"}},
        .allToolCalls = {},
    }));

    EXPECT_TRUE(result.contains(kFinalAnswer)) << "result = " << result;
    // The enriched query should contain the temporaryContext content
    EXPECT_TRUE(capturedUserContent.contains("learning guitar")) << "capturedUserContent = " << capturedUserContent;
}

// ===========================================================================
// ask – Handler: diary returns no entries → "No data was found" in tool result,
//                LLM still returns final answer
// ===========================================================================
TEST(AskTest, HandlerDiaryReturnsNoEntries) {
    auto openAI = _new<OpenAIMock>();
    DiaryMock diary;
    auto tool = tools::ask([] { return AString{}; }, openAI, diary);

    const AString kFinalAnswer = "I could not find relevant information.";

    EXPECT_CALL(*openAI, embedding(testing::_, testing::_))
        .WillOnce(Return(AFuture<std::valarray<double>>(dummyEmbedding())));

    // Empty result from diary
    EXPECT_CALL(diary, query(testing::_, testing::_))
        .WillOnce(Return(AFuture<AVector<Diary::EntryExAndRelatedness>>(AVector<Diary::EntryExAndRelatedness>{})));

    EXPECT_CALL(*openAI, chatStreaming(testing::_, testing::_))
        .WillOnce(Return(makeQueryToolCallResponse("user hobbies")))
        .WillOnce(Return(makeFinalResponse(kFinalAnswer)));

    OpenAITools tools{};

    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"query", "What are the user's hobbies and interests?"}},
        .allToolCalls = {},
    }));

    EXPECT_TRUE(result.contains(kFinalAnswer)) << "result = " << result;
}

// ===========================================================================
// ask – Tool metadata: name, description, required parameters
// ===========================================================================
TEST(AskTest, ToolMetadata) {
    auto openAI = _new<OpenAIMock>();
    DiaryMock diary;
    auto tool = tools::ask([] { return AString{}; }, openAI, diary);

    EXPECT_EQ(tool.name, "ask");
    EXPECT_FALSE(tool.description.empty());
    EXPECT_TRUE(tool.parameters.required.contains("query"));
    EXPECT_TRUE(tool.parameters.properties.contains("query"));
}
