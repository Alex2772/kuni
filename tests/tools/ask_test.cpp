//
// Created by alex2772 on 6/6/26.
//

#include "tools/ask.h"
#include "../common.h"
#include "Diary.h"
#include "IOpenAIChat.h"
#include "../OpenAIMock.h"
#include "OpenAITools.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AEventLoop.h"
#include "util/await_synchronously.h"

#include <gmock/gmock.h>

using namespace testing;

namespace {
// ---------------------------------------------------------------------------
// Mock Diary
// ---------------------------------------------------------------------------
class DiaryMock : public Diary {
public:
    explicit DiaryMock(AArc<IOpenAIChat> openAI)
        : Diary(Init{
              .diaryDir = "/tmp/kuni_test_ask_diary",
              .openAI = std::move(openAI),
          }) {}
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

// ---------------------------------------------------------------------------
// Helper: the internal #query tool computes one embedding per call
// ---------------------------------------------------------------------------
static void expectEmbeddings(OpenAIMock& openAI, int count) {
    EXPECT_CALL(openAI, embedding(testing::_, testing::_))
        .Times(count)
        .WillRepeatedly([](auto&&...) { return AFuture<std::valarray<double>>(dummyEmbedding()); });
}

} // namespace

// ===========================================================================
// ask – Handler: short query returns error, no LLM calls made
// ===========================================================================
TEST(AskTest, HandlerShortQueryError) {
    auto openAI = _new<OpenAIMock>();
    DiaryMock diary{openAI};
    auto tool = tools::ask([] { return AString{}; }, openAI, diary);

    EXPECT_CALL(*openAI, chatStreaming(testing::_, testing::_)).Times(0);
    EXPECT_CALL(*openAI, embedding(testing::_, testing::_)).Times(0);

    OpenAITools tools{};

    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"query", "short"}},
        .temporaryContext = {},
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
    DiaryMock diary{openAI};
    auto tool = tools::ask([] { return AString{}; }, openAI, diary);

    EXPECT_CALL(*openAI, chatStreaming(testing::_, testing::_)).Times(0);

    OpenAITools tools{};

    EXPECT_THROW(
        util::await_synchronously(tool.handler({
            .tools = tools,
            .args = AJson::Object{},
            .temporaryContext = {},
            .allToolCalls = {},
        })),
        AException
    );
}

// ===========================================================================
// ask – Handler: LLM makes MIN_QUERY_ROUNDS_COUNT (2) #query rounds then returns final answer
// ===========================================================================
TEST(AskTest, HandlerSuccessWithToolCall) {
    auto openAI = _new<OpenAIMock>();
    DiaryMock diary{openAI};
    auto tool = tools::ask([] { return AString{}; }, openAI, diary);

    const AString kFinalAnswer = "Alex writes ambient electronic music.";
    const AString kDiaryEntryBody = "Alex listens to ambient electronic music and writes his own tracks.";

    expectEmbeddings(*openAI, 2);

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

    EXPECT_CALL(*openAI, chatStreaming(testing::_, testing::_))
        .WillOnce(Return(makeQueryToolCallResponse("What music does Alex write?")))
        .WillOnce(Return(makeQueryToolCallResponse("Alex own tracks", "call_2")))
        .WillOnce(Return(makeFinalResponse(kFinalAnswer)));

    OpenAITools tools{};

    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"query", "What kind of music does Alex write?"}},
        .temporaryContext = {},
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
    DiaryMock diary{openAI};
    auto tool = tools::ask([] { return AString{}; }, openAI, diary);

    const AString kFinalAnswer = "Here is what I found.";

    expectEmbeddings(*openAI, 2);

    // 1st call: no tool_calls (LLM skips step) → gets "you must perform several calls" message
    // 2nd, 3rd calls: #query rounds
    // 4th call: returns final answer
    EXPECT_CALL(*openAI, chatStreaming(testing::_, testing::_))
        .WillOnce(Return(makeFinalResponse("I think I know the answer already.")))
        .WillOnce(Return(makeQueryToolCallResponse("Alex music habits")))
        .WillOnce(Return(makeQueryToolCallResponse("Alex favourite genres", "call_2")))
        .WillOnce(Return(makeFinalResponse(kFinalAnswer)));

    OpenAITools tools{};

    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"query", "Tell me about Alex's music habits in detail."}},
        .temporaryContext = {},
        .allToolCalls = {},
    }));

    EXPECT_TRUE(result.contains(kFinalAnswer)) << "result = " << result;
}

// ===========================================================================
// ask – Handler: LLM answers after a single #query round, gets reminded to pull
//                more, makes another round and returns an answer
// ===========================================================================
TEST(AskTest, HandlerLLMRemindedToQueryMore) {
    auto openAI = _new<OpenAIMock>();
    DiaryMock diary{openAI};
    auto tool = tools::ask([] { return AString{}; }, openAI, diary);

    const AString kPrematureAnswer = "Alex probably likes rock.";
    const AString kFinalAnswer = "Alex writes ambient electronic music.";

    expectEmbeddings(*openAI, 2);

    bool remindedToQueryMore = false;
    EXPECT_CALL(*openAI, chatStreaming(testing::_, testing::_))
        .WillOnce(Return(makeQueryToolCallResponse("Alex music")))
        .WillOnce(Return(makeFinalResponse(kPrematureAnswer)))
        .WillOnce([&](IOpenAIChat::Params, IOpenAIChat::Session messages) {
            remindedToQueryMore = messages.last().content.contains("pull more information");
            return makeQueryToolCallResponse("Alex own tracks", "call_2");
        })
        .WillOnce(Return(makeFinalResponse(kFinalAnswer)));

    OpenAITools tools{};

    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"query", "What kind of music does Alex write?"}},
        .temporaryContext = {},
        .allToolCalls = {},
    }));

    EXPECT_TRUE(remindedToQueryMore);
    EXPECT_TRUE(result.contains(kFinalAnswer)) << "result = " << result;
}

// ===========================================================================
// ask – Handler: with temporaryContext, query is enriched
// ===========================================================================
TEST(AskTest, HandlerWithTemporaryContextEnrichesQuery) {
    auto openAI = _new<OpenAIMock>();
    DiaryMock diary{openAI};

    AString ctxContent = "User said they are learning guitar.";

    auto tool = tools::ask([&ctxContent] { return ctxContent; }, openAI, diary);

    const AString kFinalAnswer = "Found guitar-related diary entries.";

    expectEmbeddings(*openAI, 2);

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
        .WillOnce(Return(makeQueryToolCallResponse("guitar lessons", "call_2")))
        .WillOnce(Return(makeFinalResponse(kFinalAnswer)));

    OpenAITools tools{};

    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"query", "Does Alex play any musical instruments?"}},
        .temporaryContext = {},
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
    DiaryMock diary{openAI};
    auto tool = tools::ask([] { return AString{}; }, openAI, diary);

    const AString kFinalAnswer = "I could not find relevant information.";

    expectEmbeddings(*openAI, 2);

    EXPECT_CALL(*openAI, chatStreaming(testing::_, testing::_))
        .WillOnce(Return(makeQueryToolCallResponse("user hobbies")))
        .WillOnce(Return(makeQueryToolCallResponse("user interests", "call_2")))
        .WillOnce(Return(makeFinalResponse(kFinalAnswer)));

    OpenAITools tools{};

    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"query", "What are the user's hobbies and interests?"}},
        .temporaryContext = {},
        .allToolCalls = {},
    }));

    EXPECT_TRUE(result.contains(kFinalAnswer)) << "result = " << result;
}

// ===========================================================================
// ask – Tool metadata: name, description, required parameters
// ===========================================================================
TEST(AskTest, ToolMetadata) {
    auto openAI = _new<OpenAIMock>();
    DiaryMock diary{openAI};
    auto tool = tools::ask([] { return AString{}; }, openAI, diary);

    EXPECT_EQ(tool.name, "ask");
    EXPECT_FALSE(tool.description.empty());
    EXPECT_TRUE(tool.parameters.required.contains("query"));
    EXPECT_TRUE(tool.parameters.properties.contains("query"));
}
