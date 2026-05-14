//
// Created by alex2772 on 5/9/26.
//

#include "tools/ask_google.h"
#include "../common.h"
#include "IOpenAIChat.h"
#include "OpenAITools.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AEventLoop.h"

#include <gmock/gmock.h>

using namespace testing;

namespace {
// ---------------------------------------------------------------------------
// Mock IOpenAIChat
// ---------------------------------------------------------------------------
class OpenAIMock : public IOpenAIChat {
public:
    MOCK_METHOD(AFuture<Response>, chat, (Params params, AVector<Message> messages), (override));

    ::_<StreamingResponse> chatStreaming(Params params, AVector<Message> messages) override {
        return nullptr;
    }
    MOCK_METHOD(AFuture<std::valarray<double>>, embedding, (Params params, AString input), (override));
};
}

// ===========================================================================
// askGoogle – Handler: missing query throws
// ===========================================================================
TEST(AskGoogleTest, HandlerMissingQueryThrows) {
    auto openAI = _new<OpenAIMock>();
    auto tool = tools::askGoogle(openAI);

    // No mock expectations — handler should throw before calling chat
    EXPECT_CALL(*openAI, chat(testing::_, testing::_)).Times(0);

    OpenAITools tools{};

    EXPECT_THROW(
        await(tool.handler({
            .tools = tools,
            .args = AJson::Object{},  // empty args, no "query"
            .allToolCalls = {},
        })),
        AException
    );
}

// ===========================================================================
// askGoogle – Handler: successful execution
// ===========================================================================
TEST(AskGoogleTest, HandlerSuccessIntegration) {
    auto openAI = _new<OpenAIMock>();
    auto tool = tools::askGoogle(openAI);

    const AString kSearchResult = "Found information about C++ frameworks.";

    // The web::searchAI function calls chat in a loop:
    // 1. First call: LLM returns a tool_call to #query
    // 2. handleToolCalls executes #query → calls web::search → fails (no network)
    //    The error is returned as a TOOL message
    // 3. Second call: LLM returns final answer (no tool_calls)

    IOpenAIChat::Message toolCallMessage;
    toolCallMessage.role = IOpenAIChat::Message::Role::ASSISTANT;
    toolCallMessage.content = "";
    toolCallMessage.tool_calls = {
        IOpenAIChat::Message::ToolCall{
            .id = "call_1",
            .index = 0,
            .type = "function",
            .function = {
                .name = "query",
                .arguments = R"({"text": "C++ frameworks"})",
            },
        },
    };

    IOpenAIChat::Response firstResponse;
    firstResponse.choices = {
        IOpenAIChat::Response::Choice{
            .index = 0,
            .message = std::move(toolCallMessage),
            .finish_reason = "tool_calls",
        },
    };

    IOpenAIChat::Message finalMessage;
    finalMessage.role = IOpenAIChat::Message::Role::ASSISTANT;
    finalMessage.content = kSearchResult;

    IOpenAIChat::Response secondResponse;
    secondResponse.choices = {
        IOpenAIChat::Response::Choice{
            .index = 0,
            .message = std::move(finalMessage),
            .finish_reason = "stop",
        },
    };

    // First call returns tool_call, second call returns final answer
    EXPECT_CALL(*openAI, chat(testing::_, testing::_))
        .WillOnce(Return(AFuture<IOpenAIChat::Response>(std::move(firstResponse))))
        .WillOnce(Return(AFuture<IOpenAIChat::Response>(std::move(secondResponse))));
    OpenAITools tools {};
    auto result = await(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"query", "Tell me about C++ frameworks"}},
        .allToolCalls = {},
    }));

    EXPECT_TRUE(result.contains(kSearchResult));
    EXPECT_TRUE(result.contains("Pretend you didn't Google"));
}
