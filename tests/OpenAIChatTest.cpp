#include "OpenAIChat.h"
#include <gmock/gmock.h>

#include "common.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AEventLoop.h"
#include "OpenAITools.h"
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
      "cached_tokens": 0,
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
    auto response = aui::from_json<OpenAIChat::Response>(AJson::fromString(R));
    auto content = response.choices.at(0).message.content;
    content.replaceAll("```json", "");
    content.replaceAll("```", "");
    auto json = AJson::fromString(content);
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
            "finish_reason": "tool_calls",
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
            "cached_tokens": 0,
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
    auto response = aui::from_json<OpenAIChat::Response>(AJson::fromString(R));
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
    auto response = aui::from_json<OpenAIChat::Response>(AJson::fromString(R));
    EXPECT_EQ(response.choices.at(0).message.content, "- Title: Telegram chat screenshot.");
    EXPECT_EQ(response.choices.at(0).message.reasoning, "The user wants me to describe the last photo provided.");
    EXPECT_EQ(response.choices.at(0).finish_reason, "stop");
    EXPECT_EQ(response.model, "qwen3.5:9b");
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
    auto response = aui::from_json<OpenAIChat::Response>(AJson::fromString(R));
    EXPECT_EQ(response.id, "chatcmpl-xxx");
    EXPECT_EQ(response.model, "deepseek-v4-flash");
    EXPECT_EQ(response.choices.at(0).message.content, "Ох, кто-то написал аж 3 сообщения! Давай разберёмся ^^");
    EXPECT_EQ(response.choices.at(0).message.reasoning_content, "Let me analyze the situation.\n\nHe said multiple things:\n1. About a group being dedicated to me\n2. About coffee - lighthearted\n3. About someone ignoring him - this is serious\n\nLet me respond naturally.");
    EXPECT_EQ(response.choices.at(0).finish_reason, "tool_calls");
    EXPECT_EQ(response.usage.prompt_tokens, 20301);
    EXPECT_EQ(response.usage.completion_tokens, 513);
    EXPECT_EQ(response.usage.total_tokens, 20814);
    EXPECT_EQ(response.system_fingerprint, "fp_xxx");
}
