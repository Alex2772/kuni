//
// Created by alex2772 on 2/27/26.
//

#include "OpenAIChat.h"

#include <chrono>
#include <gtest/gtest-message.h>
#include <optional>
#include <random>
#include <range/v3/algorithm/generate.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view.hpp>

#include "AUI/Curl/ACurl.h"
#include "AUI/IO/AFileOutputStream.h"
#include "AUI/Image/jpg/JpgImageLoader.h"
#include "AUI/Json/Conversion.h"
#include "AUI/Logging/ALogger.h"
#include "AUI/Reflect/AEnumerate.h"
#include "AUI/Util/kAUI.h"
#include "config.h"

static constexpr auto LOG_TAG = "OpenAIChat";

using namespace std::chrono_literals;


AJSON_FIELDS(OpenAIChat::Message::ToolCall::Function,
AJSON_FIELDS_ENTRY(name)
AJSON_FIELDS_ENTRY(arguments)
)

AJSON_FIELDS(OpenAIChat::Message::ToolCall,
AJSON_FIELDS_ENTRY(id)
AJSON_FIELDS_ENTRY(index)
AJSON_FIELDS_ENTRY(type)
AJSON_FIELDS_ENTRY(function)
)

AJSON_FIELDS(OpenAIChat::Message,
AJSON_FIELDS_ENTRY(role)
(content, "content", AJsonFieldFlags::OPTIONAL)
(reasoning, "reasoning", AJsonFieldFlags::OPTIONAL)
(tool_call_id, "tool_call_id", AJsonFieldFlags::OPTIONAL)
(tool_calls, "tool_calls", AJsonFieldFlags::OPTIONAL)
)

AJSON_FIELDS(OpenAIChat::Response::Choice,
AJSON_FIELDS_ENTRY(index)
AJSON_FIELDS_ENTRY(message)
AJSON_FIELDS_ENTRY(finish_reason)
)

AJSON_FIELDS(OpenAIChat::Response,
AJSON_FIELDS_ENTRY(id)
AJSON_FIELDS_ENTRY(object)
AJSON_FIELDS_ENTRY(created)
AJSON_FIELDS_ENTRY(model)
AJSON_FIELDS_ENTRY(system_fingerprint)
AJSON_FIELDS_ENTRY(choices)
AJSON_FIELDS_ENTRY(usage)
)

AJSON_FIELDS(OpenAIChat::Response::Usage,
AJSON_FIELDS_ENTRY(prompt_tokens)
AJSON_FIELDS_ENTRY(completion_tokens)
AJSON_FIELDS_ENTRY(total_tokens)

)

AFuture<OpenAIChat::Response> OpenAIChat::chat(AString message) {
  return chat({
    { Message::Role::USER, std::move(message)} ,
  });
}

template<>
struct AJsonConv<OpenAIChat::Message::Attachment> {
    static AJson toJson(const OpenAIChat::Message::Attachment& v) {
        return std::visit(aui::lambda_overloaded {
            [](const _<AImage>& image) -> AJson {
                AByteBuffer jpg;
                JpgImageLoader::save(jpg, AImageView(*image));
                return AJson::Object {
                    {"type", "image_url"},
                    {"image_url", "data:image/jpg;base64,{}"_format(jpg.toBase64String())}
                };
            },
        }, v);
    }
};

template<>
struct AJsonConv<AVector<OpenAIChat::Message>> {
    static AJson toJson(const AVector<OpenAIChat::Message>& v) {
        AJson::Array result;
        for (const auto& message : v) {
            // reverse engineered from vscode copilot plugin
            if (!message.attachments.empty()) {
                result << aui::to_json(OpenAIChat::Message{
                    .role = message.role,
                    .content = "<attachments>",
                });
                result << AJson::Object {
                    { "role", aui::to_json(message.role) },
                    { "content", aui::to_json(message.attachments) },
                };
                result << aui::to_json(OpenAIChat::Message{
                    .role = message.role,
                    .content = "</attachments>",
                });
            }
            result << aui::to_json(message);
        }
        return result;
    }
};


AFuture<OpenAIChat::Response> OpenAIChat::chat(AVector<Message> messages) {
  messages.insert(messages.begin(), {Message::Role::SYSTEM_PROMPT, systemPrompt});
  auto query = AJson::toString({
      {
        "messages", aui::to_json(messages),
      },
      { "stream", false },
      { "use_context", false },
      { "include_sources", true },
      { "model", config::MODEL },
      { "tools", tools },
    });
  AFileOutputStream("query.json") << query.toStdString();
  ALOG_DEBUG(LOG_TAG) << "Query: " << query;
  auto response = AJson::fromBuffer((co_await ACurl::Builder(baseUrl + "v1/chat/completions")
    .withMethod(ACurl::Method::HTTP_POST)
    .withTimeout(4h)
    .withHeaders({"Content-Type: application/json"})
    .withBody(query.toStdString()).runAsync()).body);
  ALOG_DEBUG(LOG_TAG) << "Response: " << AJson::toString(response);
  co_return aui::from_json<Response>(response);
};
