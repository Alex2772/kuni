#include "IOpenAIChat.h"
#include "OpenAITools.h"
#include <gmock/gmock.h>

#include "common.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AEventLoop.h"
#include "config.h"
#include "util/cosine_similarity.h"


TEST(OpenAIChat, ParseResponseOpenRouter1) {
    static constexpr auto R = R"(
{
  "id": "gen-1777241316-pffm5r7c8ACtvqbrZqXm",
  "object": "chat.completion",
  "created": 1777241316,
  "model": "google/gemma-3-12b-it",
  "provider": "DeepInfra",
  "system_fingerprint": null,
  "choices": [
    {
      "index": 0,
      "logprobs": null,
      "finish_reason": "stop",
      "native_finish_reason": "stop",
      "message": {
        "role": "assistant",
        "content": "```json\n{\n  \"positivePrompt\": \"Anime girl, cat ears, shoulder-length white hair, messy strands, gold eyes, small nose, cute fangs, bare shoulders and chest, playful expression, leaning forward, soft lighting from window, floating particles in the air, dark corset with gold lace trim, thighhigh stockings with lace trim, delicate collarbones, beauty mark under left eye, rustic interior, wooden beams, selfie, (age_30:1.2), medium breasts, <lora:perfecteyes:1>, <lora:Iridescence:1>\",\n  \"negativePrompt\": \"(text:2), (signature:2), raw photo, group photo, multiple people\"\n}\n```",
        "refusal": null,
        "reasoning": null
      }
    }
  ],
  "usage": {
    "prompt_tokens": 1113,
    "completion_tokens": 152,
    "total_tokens": 1265,
    "cost": 0.000064,
    "is_byok": false,
    "prompt_tokens_details": {
      "cached_tokens": 1000,
      "cache_write_tokens": 0,
      "audio_tokens": 0,
      "video_tokens": 0
    },
    "cost_details": {
      "upstream_inference_cost": 0.000064,
      "upstream_inference_prompt_cost": 0.000045,
      "upstream_inference_completions_cost": 0.00002
    },
    "completion_tokens_details": {
      "reasoning_tokens": 0,
      "image_tokens": 0,
      "audio_tokens": 0
    }
  }
}
)";
    auto response = aui::from_json<IOpenAIChat::Response>(AJson::fromString(R));
    auto content = response.choices.at(0).message.content;
    content.replaceAll("```json", "");
    content.replaceAll("```", "");
    auto json = AJson::fromString(content);
    
    EXPECT_EQ(response.model, "google/gemma-3-12b-it");
    EXPECT_EQ(response.choices.at(0).finish_reason, "stop");
    ASSERT_TRUE(json.isObject());
    EXPECT_TRUE(json["positivePrompt"].asString().contains("Anime girl"));
    EXPECT_TRUE(json["negativePrompt"].asString().contains("text:2"));
}

TEST(OpenAIChat, ParseResponseOpenRouter2) {
    static constexpr auto R = R"(
{
    "id": "gen-1777578167-jZzu89fMce7HQBlRCaAd",
    "object": "chat.completion",
    "created": 1777578167,
    "model": "deepseek/deepseek-v4-flash-20260423",
    "provider": "DeepInfra",
    "system_fingerprint": null,
    "choices": [
        {
            "index": 0,
            "logprobs": null,
            "finish_reason": null,
            "native_finish_reason": "tool_calls",
            "message": {
                "role": "assistant",
                "content": "\n\n\n",
                "refusal": null,
                "reasoning": null,
                "tool_calls": [
                    {
                        "type": "function",
                        "index": 0,
                        "id": "call_5354120bc0f54cb8b688637e",
                        "function": {
                            "name": "send_telegram_message",
                            "arguments": "{\"reply_to_message_id\": 123, \"text\": \"прости, я не вижу картинку почему-то! 😅 может она не загрузилась у меня? попробуй ещё раз скинуть? 👀🌸\"}"
                        }
                    }
                ]
            }
        }
    ],
    "usage": {
        "prompt_tokens": 13312,
        "completion_tokens": 106,
        "total_tokens": 13418,
        "cost": 0.001893,
        "is_byok": false,
        "prompt_tokens_details": {
            "cached_tokens": 1000,
            "cache_write_tokens": 0,
            "audio_tokens": 0,
            "video_tokens": 0
        },
        "cost_details": {
            "upstream_inference_cost": 0.001893,
            "upstream_inference_prompt_cost": 0.001864,
            "upstream_inference_completions_cost": 0.00003
        },
        "completion_tokens_details": {
            "reasoning_tokens": 0,
            "image_tokens": 0,
            "audio_tokens": 0
        }
    }
}
)";
    auto response = aui::from_json<IOpenAIChat::Response>(AJson::fromString(R));
    
    EXPECT_EQ(response.model, "deepseek/deepseek-v4-flash-20260423");
    EXPECT_EQ(response.usage.prompt_tokens, 13312);
    EXPECT_EQ(response.usage.completion_tokens, 106);
    EXPECT_EQ(response.usage.total_tokens, 13418);
    EXPECT_EQ(response.usage.prompt_cache_hit_tokens, 1000);
    EXPECT_EQ(response.usage.prompt_cache_miss_tokens, 12312);
    EXPECT_EQ(response.choices.at(0).finish_reason, "");
    ASSERT_EQ(response.choices.at(0).message.tool_calls.size(), 1);
    EXPECT_EQ(response.choices.at(0).message.tool_calls[0].function.name, "send_telegram_message");
    
    AOptional<AJson> args;
    OpenAITools tools {
        {
            .name = "send_telegram_message",
            .handler = [&](OpenAITools::Ctx ctx) -> AFuture<AString> {
                args = std::move(ctx.args);
                co_return "Success";
            },
        },
    };
    *tools.handleToolCalls(response.choices.at(0).message.tool_calls);
    ASSERT_TRUE(args.hasValue());
    EXPECT_EQ((*args)["reply_to_message_id"].asLongInt(), 123);
    EXPECT_EQ((*args)["text"].asString(), "прости, я не вижу картинку почему-то! 😅 может она не загрузилась у меня? попробуй ещё раз скинуть? 👀🌸");
}

TEST(OpenAIChat, ParseResponseOllama1) {
    static constexpr auto R = R"(
{
  "id": "chatcmpl-308",
  "object": "chat.completion",
  "created": 1777529963,
  "model": "qwen3.5:9b",
  "system_fingerprint": "fp_ollama",
  "choices": [
    {
      "index": 0,
      "message": {
        "role": "assistant",
        "content": "- Title: Telegram chat screenshot.",
        "reasoning": "The user wants me to describe the last photo provided."
      },
      "finish_reason": "stop"
    }
  ],
  "usage": {
    "prompt_tokens": 6863,
    "completion_tokens": 1676,
    "total_tokens": 8539
  }
}
)";
    auto response = aui::from_json<IOpenAIChat::Response>(AJson::fromString(R));
    EXPECT_EQ(response.choices.at(0).message.content, "- Title: Telegram chat screenshot.");
    EXPECT_EQ(response.choices.at(0).message.reasoning, "The user wants me to describe the last photo provided.");
    EXPECT_EQ(response.choices.at(0).finish_reason, "stop");
    EXPECT_EQ(response.model, "qwen3.5:9b");
    EXPECT_EQ(response.usage.prompt_tokens, 6863);
    EXPECT_EQ(response.usage.completion_tokens, 1676);
    EXPECT_EQ(response.usage.total_tokens, 8539);
    EXPECT_EQ(response.usage.prompt_cache_hit_tokens, 0);
    EXPECT_EQ(response.usage.prompt_cache_miss_tokens, 0);
}

TEST(OpenAIChat, ParseResponseDeepseek1) {
    static constexpr auto R = R"(
{
  "id": "chatcmpl-xxx",
  "object": "chat.completion",
  "created": 1777579578,
  "model": "deepseek-v4-flash",
  "choices": [
    {
      "index": 0,
      "message": {
        "role": "assistant",
        "content": "Ох, кто-то написал аж 3 сообщения! Давай разберёмся ^^",
        "reasoning_content": "Let me analyze the situation.\n\nHe said multiple things:\n1. About a group being dedicated to me\n2. About coffee - lighthearted\n3. About someone ignoring him - this is serious\n\nLet me respond naturally."
      },
      "logprobs": null,
      "finish_reason": "tool_calls"
    }
  ],
  "usage": {
    "prompt_tokens": 20301,
    "completion_tokens": 513,
    "total_tokens": 20814,
    "prompt_tokens_details": {
      "cached_tokens": 4096
    },
    "completion_tokens_details": {
      "reasoning_tokens": 341
    },
    "prompt_cache_hit_tokens": 4096,
    "prompt_cache_miss_tokens": 16205
  },
  "system_fingerprint": "fp_xxx"
}
)";
    auto response = aui::from_json<IOpenAIChat::Response>(AJson::fromString(R));
    EXPECT_EQ(response.id, "chatcmpl-xxx");
    EXPECT_EQ(response.model, "deepseek-v4-flash");
    EXPECT_EQ(response.choices.at(0).message.content, "Ох, кто-то написал аж 3 сообщения! Давай разберёмся ^^");
    EXPECT_EQ(response.choices.at(0).message.reasoning_content, "Let me analyze the situation.\n\nHe said multiple things:\n1. About a group being dedicated to me\n2. About coffee - lighthearted\n3. About someone ignoring him - this is serious\n\nLet me respond naturally.");
    EXPECT_EQ(response.choices.at(0).finish_reason, "tool_calls");
    EXPECT_EQ(response.usage.prompt_tokens, 20301);
    EXPECT_EQ(response.usage.completion_tokens, 513);
    EXPECT_EQ(response.usage.total_tokens, 20814);
    EXPECT_EQ(response.usage.prompt_cache_hit_tokens, 4096);
    EXPECT_EQ(response.usage.prompt_cache_miss_tokens, 16205);
    EXPECT_EQ(response.system_fingerprint, "fp_xxx");
}

// =====================================================================
// IOpenAIChat::Message::operator+= (streaming merge)
// =====================================================================

TEST(OpenAIChat, StreamingMessageMerge) {
    IOpenAIChat::Message base{
        .role = IOpenAIChat::Message::Role::ASSISTANT,
        .content = "Hello",
        .reasoning = "Think",
    };
    IOpenAIChat::Message delta{
        .role = IOpenAIChat::Message::Role::ASSISTANT,
        .content = " world",
        .reasoning = "ing",
    };
    base += delta;
    EXPECT_EQ(base.content, "Hello world");
    EXPECT_EQ(base.reasoning, "Thinking");
}

TEST(OpenAIChat, StreamingMessageMergeEmptyDelta) {
    IOpenAIChat::Message base{
        .role = IOpenAIChat::Message::Role::ASSISTANT,
        .content = "Hello",
    };
    IOpenAIChat::Message delta{
        .role = IOpenAIChat::Message::Role::ASSISTANT,
        .content = "",
    };
    base += delta;
    EXPECT_EQ(base.content, "Hello");
}

TEST(OpenAIChat, StreamingToolCallMerge) {
    IOpenAIChat::Message base;
    base.tool_calls = {
        { .id = "call_1", .index = 0, .function = { .name = "test", .arguments = "{\"a\"" } }
    };
    IOpenAIChat::Message delta;
    delta.tool_calls = {
        { .id = "", .index = 0, .function = { .name = "", .arguments = ": 1}" } }
    };
    base += delta;
    ASSERT_EQ(base.tool_calls.size(), 1);
    EXPECT_EQ(base.tool_calls[0].function.arguments, "{\"a\": 1}");
}

TEST(OpenAIChat, StreamingToolCallMultipleIndices) {
    IOpenAIChat::Message base;
    base.tool_calls = {
        { .id = "call_0", .index = 0, .function = { .name = "fn1", .arguments = "{}" } }
    };
    IOpenAIChat::Message delta;
    delta.tool_calls = {
        { .id = "call_1", .index = 1, .function = { .name = "fn2", .arguments = "{}" } }
    };
    base += delta;
    ASSERT_EQ(base.tool_calls.size(), 2);
    EXPECT_EQ(base.tool_calls[0].function.name, "fn1");
    EXPECT_EQ(base.tool_calls[1].function.name, "fn2");
}

TEST(OpenAIChat, StreamingMergePreservesRole) {
    IOpenAIChat::Message base{
        .role = IOpenAIChat::Message::Role::ASSISTANT,
        .content = "Hello",
    };
    IOpenAIChat::Message delta{
        .role = IOpenAIChat::Message::Role::USER, // role should be overwritten by delta
        .content = " world",
    };
    base += delta;
    EXPECT_EQ(base.role, IOpenAIChat::Message::Role::USER);
    EXPECT_EQ(base.content, "Hello world");
}

// =====================================================================
// IOpenAIChat::Message::Role JSON conversion
// =====================================================================

TEST(OpenAIChat, RoleJsonRoundtrip) {
    auto testRole = [](IOpenAIChat::Message::Role role, const char* expected) {
        auto json = aui::to_json(role);
        EXPECT_EQ(json.asString(), expected);
        IOpenAIChat::Message::Role decoded;
        AJsonConv<IOpenAIChat::Message::Role>::fromJson(json, decoded);
        EXPECT_EQ(decoded, role);
    };
    testRole(IOpenAIChat::Message::Role::USER, "user");
    testRole(IOpenAIChat::Message::Role::ASSISTANT, "assistant");
    testRole(IOpenAIChat::Message::Role::SYSTEM_PROMPT, "system");
    testRole(IOpenAIChat::Message::Role::TOOL, "tool");
}

// =====================================================================
// IOpenAIChat::String JSON conversion (null handling)
// =====================================================================

TEST(OpenAIChat, NullStringConversion) {
    AJson nullJson = AJson{nullptr};
    IOpenAIChat::String out;
    AJsonConv<IOpenAIChat::String>::fromJson(nullJson, out);
    EXPECT_TRUE(out.empty());
}

TEST(OpenAIChat, NormalStringConversion) {
    AJson strJson = "hello";
    IOpenAIChat::String out;
    AJsonConv<IOpenAIChat::String>::fromJson(strJson, out);
    EXPECT_EQ(out, "hello");
}

// =====================================================================
// OpenAITools::asJson() serialization
// =====================================================================

TEST(OpenAITools, ToJsonBasic) {
    OpenAITools tools = {
        {
            .name = "test_tool",
            .description = "A test tool",
            .parameters = {
                .properties = {
                    {"param1", { .type = "string", .description = "A param" }},
                },
                .required = {"param1"},
            },
            .handler = [](OpenAITools::Ctx) -> AFuture<AString> { co_return "ok"; },
        }
    };
    auto json = tools.asJson();
    ASSERT_TRUE(json.isArray());
    ASSERT_GE(json.asArray().size(), 1);
    EXPECT_EQ(json[0]["function"]["name"].asString(), "test_tool");
    EXPECT_EQ(json[0]["function"]["description"].asString(), "A test tool");
    EXPECT_EQ(json[0]["function"]["parameters"]["type"].asString(), "object");
    EXPECT_EQ(json[0]["function"]["parameters"]["required"][0].asString(), "param1");
    EXPECT_EQ(json[0]["function"]["parameters"]["properties"]["param1"]["type"].asString(), "string");
    EXPECT_EQ(json[0]["function"]["strict"].asBool(), true);
}

TEST(OpenAITools, ToJsonMultipleTools) {
    OpenAITools tools = {
        {
            .name = "tool_a",
            .description = "First tool",
            .handler = [](OpenAITools::Ctx) -> AFuture<AString> { co_return "a"; },
        },
        {
            .name = "tool_b",
            .description = "Second tool",
            .handler = [](OpenAITools::Ctx) -> AFuture<AString> { co_return "b"; },
        },
    };
    auto json = tools.asJson();
    ASSERT_TRUE(json.isArray());
    ASSERT_EQ(json.asArray().size(), 2);
    // Order may vary (AMap), so check by name
    auto names = AVector<AString>{json[0]["function"]["name"].asString(), json[1]["function"]["name"].asString()};
    EXPECT_TRUE(names.contains("tool_a"));
    EXPECT_TRUE(names.contains("tool_b"));
}

TEST(OpenAITools, ToJsonNoParameters) {
    OpenAITools tools = {
        {
            .name = "simple_tool",
            .description = "No params needed",
            .handler = [](OpenAITools::Ctx) -> AFuture<AString> { co_return "ok"; },
        }
    };
    auto json = tools.asJson();
    ASSERT_TRUE(json.isArray());
    ASSERT_GE(json.asArray().size(), 1);
    EXPECT_EQ(json[0]["function"]["name"].asString(), "simple_tool");
    // Should still have parameters object with type "object"
    EXPECT_EQ(json[0]["function"]["parameters"]["type"].asString(), "object");
}

// =====================================================================
// IOpenAIChat::Message serialization to JSON (without embedding tags)
// =====================================================================

TEST(OpenAIChat, MessageToJsonSimple) {
    IOpenAIChat::Message msg{
        .role = IOpenAIChat::Message::Role::USER,
        .content = "plain text message",
    };
    auto json = aui::to_json(AVector<IOpenAIChat::Message>{msg});
    ASSERT_TRUE(json.isArray());
    ASSERT_EQ(json.asArray().size(), 1);
    EXPECT_EQ(json[0]["role"].asString(), "user");
    EXPECT_EQ(json[0]["content"].asString(), "plain text message");
}

TEST(OpenAIChat, MessageToJsonAssistantWithReasoning) {
    IOpenAIChat::Message msg{
        .role = IOpenAIChat::Message::Role::ASSISTANT,
        .content = "response",
        .reasoning = "thinking process",
    };
    auto json = aui::to_json(AVector<IOpenAIChat::Message>{msg});
    ASSERT_TRUE(json.isArray());
    ASSERT_EQ(json.asArray().size(), 1);
    EXPECT_EQ(json[0]["role"].asString(), "assistant");
    EXPECT_EQ(json[0]["content"].asString(), "response");
    EXPECT_EQ(json[0]["reasoning"].asString(), "thinking process");
}

TEST(OpenAIChat, MessageToJsonToolCall) {
    IOpenAIChat::Message msg{
        .role = IOpenAIChat::Message::Role::ASSISTANT,
        .content = "",
        .tool_calls = {
            {
                .id = "call_1",
                .index = 0,
                .type = "function",
                .function = { .name = "test_tool", .arguments = "{}" },
            }
        },
    };
    auto json = aui::to_json(AVector<IOpenAIChat::Message>{msg});
    ASSERT_TRUE(json.isArray());
    ASSERT_EQ(json.asArray().size(), 1);
    EXPECT_EQ(json[0]["role"].asString(), "assistant");
    ASSERT_TRUE(json[0]["tool_calls"].isArray());
    EXPECT_EQ(json[0]["tool_calls"][0]["id"].asString(), "call_1");
    EXPECT_EQ(json[0]["tool_calls"][0]["function"]["name"].asString(), "test_tool");
}
