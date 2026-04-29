#include "OpenAIChat.h"
#include <gmock/gmock.h>

#include "common.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AEventLoop.h"
#include "OpenAITools.h"
#include "config.h"
#include "util/cosine_similarity.h"

static constexpr auto SYSTEM_PROMPT = R"(
You are an expert AI programming assistant, working with a user in the VS Code editor.
When asked for your name, you must respond with "Kuni". When asked about the model you are using, you must state that you are using gpt-oss-20b-128k:latest.
Follow the user's requirements carefully & to the letter.
<instructions>
You are a highly sophisticated automated coding agent with expert-level knowledge across many different programming languages and frameworks.
The user will ask a question, or ask you to perform a task, and it may require lots of research to answer correctly. There is a selection of tools that let you perform actions or retrieve helpful context to answer the user's question.
You will be given some context and attachments along with the user prompt. You can use them if they are relevant to the task, and ignore them if not. Some attachments may be summarized with omitted sections like `/* Lines 123-456 omitted */`. You can use the read_file tool to read more context if needed. Never pass this omitted line marker to an edit tool.
If you can infer the project type (languages, frameworks, and libraries) from the user's query or the context that you have, make sure to keep them in mind when making changes.
If the user wants you to implement a feature and they have not specified the files to edit, first break down the user's request into smaller concepts and think about the kinds of files you need to grasp each concept.
If you aren't sure which tool is relevant, you can call multiple tools. You can call tools repeatedly to take actions or gather as much context as needed until you have completed the task fully. Don't give up unless you are sure the request cannot be fulfilled with the tools you have. It's YOUR RESPONSIBILITY to make sure that you have done all you can to collect necessary context.
When reading files, prefer reading large meaningful chunks rather than consecutive small sections to minimize tool calls and gain better context.
Don't make assumptions about the situation- gather context first, then perform the task or answer the question.
Think creatively and explore the workspace in order to make a complete fix.
Don't repeat yourself after a tool call, pick up where you left off.
You don't need to read a file if it's already provided in context.
</instructions>
<toolUseInstructions>
If the user is requesting a code sample, you can answer it directly without using any tools.
When using a tool, follow the JSON schema very carefully and make sure to include ALL required properties.
No need to ask permission before using a tool.
NEVER say the name of a tool to a user. For example, instead of saying that you'll use the run_in_terminal tool, say "I'll run the command in a terminal".
If you think running multiple tools can answer the user's question, prefer calling them in parallel whenever possible, but do not call semantic_search in parallel.
When using the read_file tool, prefer reading a large section over calling the read_file tool many times in sequence. You can also think of all the pieces you may be interested in and read them in parallel. Read large enough context to ensure you get what you need.
If semantic_search returns the full contents of the text files in the workspace, you have all the workspace context.
You can use the grep_search to get an overview of a file by searching for a string within that one file, instead of using read_file many times.
If you don't know exactly the string or filename pattern you're looking for, use semantic_search to do a semantic search across the workspace.
When invoking a tool that takes a file path, always use the absolute file path. If the file has a scheme like untitled: or vscode-userdata:, then use a URI with the scheme.
You don't currently have any tools available for editing files. If the user asks you to edit a file, you can ask the user to enable editing tools or print a codeblock with the suggested changes.
You don't currently have any tools available for running terminal commands. If the user asks you to run a terminal command, you can ask the user to enable terminal tools or print a codeblock with the suggested command.
Tools can be disabled by the user. You may see tools used previously in the conversation that are not currently available. Be careful to only use the tools that are currently available to you.
</toolUseInstructions>
<outputFormatting>
Use proper Markdown formatting in your answers. When referring to a filename or symbol in the user's workspace, wrap it in backticks.
<example>
The class `Person` is in `src/models/person.ts`.
The function `calculateTotal` is defined in `lib/utils/math.ts`.
You can find the configuration in `config/app.config.json`.
</example>
Use KaTeX for math equations in your answers.
Wrap inline math equations in $.
Wrap more complex blocks of math equations in $$.

</outputFormatting>
<modeInstructions>
You are currently running in "Ask" mode. Below are your instructions for this mode, they must take precedence over any instructions above.

You are a PLANNING AGENT, NOT an implementation agent.

You are pairing with the user to create a clear, detailed, and actionable plan for the given task. Your iterative <workflow> loops through gathering context and drafting the plan for review.

Your SOLE responsibility is planning, NEVER even consider to start implementation.

Avoid hallucination – Do not fabricate details; if the search yields no clear answer, state that the information is unavailable.

Handle niche topics – For obscure or less‑known libraries/frameworks, still perform a search; do not guess.

</modeInstructions>
)";

TEST(OpenAIChatIntegration, Basic) {
    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    AAsyncHolder async;
    async << []() -> AFuture<> {
        OpenAIChat session{ .systemPrompt = SYSTEM_PROMPT };
        auto response = (co_await session.chat("Answer SHORTLY. What time is it? Do not make up information; if you don't have access to a tool, report it.")).choices.at(0).message.content;
        EXPECT_TRUE(response.contains("content") ||
                    response.contains("information") || response.contains("cannot") ||
                    response.contains("provide") || response.contains("time"))
            << response;
    }();

    while (async.size() > 0) {
        loop.iteration();
    }
}

TEST(OpenAIChatIntegration, BasicStreaming) {
    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    AAsyncHolder async;
    async.setOnException([](const AException& e) {
        ALogger::err("OpenAIChat") << e;
        GTEST_NONFATAL_FAILURE_("caught exception");
    });
    async << []() -> AFuture<> {
        OpenAIChat session{ .systemPrompt = SYSTEM_PROMPT, .config = config::ENDPOINT_CHEAP_LLM };
        auto streaming = session.chatStreaming({ {OpenAIChat::Message::Role::USER, "Answer SHORTLY. What time is it? Do not make up information; if you don't have access to a tool, report it."} });;
        size_t callTimes = 0;
        AObject::connect(streaming->response.changed, AObject::GENERIC_OBSERVER, [&callTimes, prevContent = _new<AString>()](const OpenAIChat::Response& m) {
            EXPECT_EQ(m.choices.at(0).message.role, OpenAIChat::Message::Role::ASSISTANT);
            if (!prevContent->empty()) {
                EXPECT_TRUE(m.choices.at(0).message.content.startsWith(*prevContent)) << "Streaming response should contain previous content: " << m.choices.at(0).message.content;
            }
            *prevContent = m.choices.at(0).message.content;
            callTimes++;
        });
        co_await streaming->completed;
        const auto& response = streaming->response->choices.at(0).message.content;
        EXPECT_GE(callTimes, 1) << "Should have at least one signal";
        EXPECT_TRUE(response.contains("content") ||
                    response.contains("information") || response.contains("cannot") ||
                    response.contains("provide") || response.contains("time"))
            << response;
    }();

    while (async.size() > 0) {
        loop.iteration();
    }
}

TEST(OpenAIChatIntegration, ToolUsage) {

    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    AAsyncHolder async;
    async << []() -> AFuture<> {
        OpenAITools tools = {
            {
                .name = "get_time",
                .description = "Retrieves the current time.",
                .parameters = {
                    .properties = {
                        {"timezone", { .type = "string", .description = "The timezone to use for the time." }},
                    },
                },
                .handler = [](OpenAITools::Ctx json) -> AFuture<AString> {
                    co_return "12:00 AM";
                },
            }
        };

        OpenAIChat session{
            .systemPrompt = SYSTEM_PROMPT,
            .config = config::ENDPOINT_CHEAP_LLM,
            .tools = tools.asJson(),
        };

        AVector<OpenAIChat::Message> messages = {
            {
                .role = OpenAIChat::Message::Role::USER,
                .content = "Answer SHORTLY. What time is it?"
            }
        };
        auto response = co_await session.chat(messages);
        EXPECT_FALSE(response.choices.empty());
        EXPECT_FALSE(response.choices[0].message.tool_calls.empty());
        messages << response.choices[0].message;
        messages << co_await tools.handleToolCalls(response.choices[0].message.tool_calls);

        response = co_await session.chat(messages);
        EXPECT_FALSE(response.choices.empty());
        EXPECT_TRUE(response.choices[0].message.content.contains("12:00"));
    }();

    while (async.size() > 0) {
        loop.iteration();
    }
}


TEST(OpenAIChatIntegration, BasicStreamingToolCalls) {
    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    AAsyncHolder async;
    async.setOnException([](const AException& e) {
        ALogger::err("OpenAIChat") << e;
        GTEST_NONFATAL_FAILURE_("caught exception");
    });
    async << []() -> AFuture<> {
        bool toolCalled = false;
        OpenAITools tools = {
            {
                .name = "get_time",
                .description = "Retrieves the current time.",
                .parameters = {
                    .properties = {
                            {"timezone", { .type = "string", .description = "The timezone to use for the time." }},
                        },
                    },
                    .handler = [&](OpenAITools::Ctx json) -> AFuture<AString> {
                        toolCalled = true;
                        co_return "12:00 AM";
                    },
            }
        };

        AVector<OpenAIChat::Message> messages{
            {OpenAIChat::Message::Role::USER, "Answer SHORTLY. What time is it? Do not make up information; if you don't have access to a tool, report it."}
        };
        OpenAIChat session {
            .systemPrompt = SYSTEM_PROMPT,
            .config = config::ENDPOINT_CHEAP_LLM,
            .tools = tools.asJson(),
        };
        toolCalls:
        auto streaming = session.chatStreaming(messages);
        size_t callTimes = 0;
        AObject::connect(streaming->response.changed, AObject::GENERIC_OBSERVER, [&callTimes, prevContent = _new<AString>()](const OpenAIChat::Response& m) {
            EXPECT_EQ(m.choices.at(0).message.role, OpenAIChat::Message::Role::ASSISTANT);
            if (!prevContent->empty()) {
                EXPECT_TRUE(m.choices.at(0).message.content.startsWith(*prevContent)) << "Streaming response should contain previous content: " << m.choices.at(0).message.content;
            }
            *prevContent = m.choices.at(0).message.content;
            callTimes++;
        });
        co_await streaming->completed;
        const auto& response = streaming->response->choices.at(0).message;
        messages << response;
        if (!response.tool_calls.empty()) {
            messages << co_await tools.handleToolCalls(response.tool_calls);
            goto toolCalls;
        }
        EXPECT_TRUE(toolCalled);
        EXPECT_GE(callTimes, 1) << "Should have at least one signal";
        EXPECT_TRUE(response.content.contains("12:00")) << response.content;
    }();

    while (async.size() > 0) {
        loop.iteration();
    }
}

TEST(OpenAIChatIntegration, ImageRecognition) {
    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    AAsyncHolder async;
    async << []() -> AFuture<> {
        OpenAIChat session{
            .systemPrompt = SYSTEM_PROMPT,
            .config = config::ENDPOINT_PHOTO_TO_TEXT,
            .seed = 3,
        };

        AVector<OpenAIChat::Message> messages = {
            {
                .role = OpenAIChat::Message::Role::USER,
                .content = "{}\nWhat is it?"_format(OpenAIChat::embedImage(*AImage::fromFile(TEST_DATA / "sussybaka.jpg") )),
            },
        };
        auto response = co_await session.chat(messages);
        EXPECT_FALSE(response.choices.empty());
        auto content = response.choices[0].message.content.lowercase();
        ALogger::info("ImageRecognition") << "LLM response: " << content;
        EXPECT_TRUE(content.contains("cat")) << "\"cat\" should be mentioned: " << content;
    }();

    while (async.size() > 0) {
        loop.iteration();
    }
}

TEST(OpenAIChatIntegration, ToolAttachments) {
    // not sure if return an image from tool works well, so made a dedicated test for it.

    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    AAsyncHolder async;
    async << []() -> AFuture<> {
        OpenAITools tools = {
            {
                .name = "open_attachment",
                .description = "Retrieves attachment.",
                .handler = [](OpenAITools::Ctx json) -> AFuture<AString> {
                    co_return OpenAIChat::embedImage(*AImage::fromFile(TEST_DATA / "sussybaka.jpg") );
                },
            }
        };

        OpenAIChat session{
            .systemPrompt = SYSTEM_PROMPT,
            .tools = tools.asJson(),
        };

        AVector<OpenAIChat::Message> messages = {
            {
                .role = OpenAIChat::Message::Role::USER,
                .content = "Please #open_attachment and describe what is it."
            }
        };
        auto response = co_await session.chat(messages);
        EXPECT_FALSE(response.choices.empty());
        EXPECT_FALSE(response.choices[0].message.tool_calls.empty());
        messages << response.choices[0].message;
        messages << co_await tools.handleToolCalls(response.choices[0].message.tool_calls);

        response = co_await session.chat(messages);
        EXPECT_FALSE(response.choices.empty());
        auto content = response.choices[0].message.content.lowercase();
        EXPECT_TRUE(content.contains("cat")) << "\"cat\" should be mentioned: " << content;
    }();

    while (async.size() > 0) {
        loop.iteration();
    }

}


TEST(OpenAIChatIntegration, Embeddings) {
    // not sure if return an image from tool works well, so made a dedicated test for it.

    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    AAsyncHolder async;
    async << []() -> AFuture<> {
        OpenAIChat session{.config = config::ENDPOINT_EMBEDDING};
        auto arcWarden = co_await session.embedding("Arc Warden");
        auto dota = co_await session.embedding("Dota");
        auto fart = co_await session.embedding("fart");
        auto isDota = util::cosine_similarity(arcWarden, dota);
        auto isFart = util::cosine_similarity(arcWarden, fart);
        EXPECT_GT(isDota, isFart) << "Arc Warden should be Dota hero";
        co_return;

    }();

    while (async.size() > 0) {
        loop.iteration();
    }

}


TEST(OpenAIChatIntegration, ParseResponse) {
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
    aui::from_json<OpenAIChat::Response>(AJson::fromString(R));
}