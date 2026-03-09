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
struct AJsonConv<AVector<OpenAIChat::Message>> {
    static AJson toJson(const AVector<OpenAIChat::Message>& v) {
        AJson::Array result;
        for (const auto& message : v) {
            // reverse engineered from vscode copilot plugin
            if (message.content.contains("</{}>"_format(OpenAIChat::EMBEDDING_TAG))) {
                auto content = std::string_view(message.content);
                auto append = [&](const OpenAIChat::Message& msg) {
                    if (msg.content.empty()) {
                        return;
                    }
                    result << aui::to_json(msg);
                };
                for (;;) {
                    auto tagPos = content.find("<{}>"_format(OpenAIChat::EMBEDDING_TAG));
                    append(OpenAIChat::Message{
                        .role = message.role,
                        .content = content.substr(0, tagPos),
                    });
                    if (tagPos == std::string::npos) {
                        break;
                    }
                    content = content.substr(tagPos);
                    content = content.substr(content.find(">") + 1);
                    auto body = content.substr(0, content.find("</{}>"_format(OpenAIChat::EMBEDDING_TAG)));
                    append(OpenAIChat::Message{
                        .role = message.role,
                        .content = "<attachments>",
                    });
                    result << AJson::Object{
                        {"role", aui::to_json(message.role)},
                        {"content",
                         AJson::Array{
                             AJson::Object{{"type", "image_url"},
                                           {"image_url", body}},
                         }},
                    };
                    append(OpenAIChat::Message{
                        .role = message.role,
                        .content = "</attachments>",
                    });
                    content = content.substr(body.length());
                    content = content.substr(content.find(">") + 1);
                }
                continue;
            }
            result << aui::to_json(message);
        }
        return result;
    }
};


AString OpenAIChat::embedImage(AImageView image) {
    AByteBuffer jpg;
    JpgImageLoader::save(jpg, image);
    return "<{}>data:image/jpg;base64,{}</{}>"_format(EMBEDDING_TAG, jpg.toBase64String(), EMBEDDING_TAG);
}


AFuture<OpenAIChat::Response> OpenAIChat::chat(AVector<Message> messages) {
  messages.insert(messages.begin(), {Message::Role::SYSTEM_PROMPT, systemPrompt});
  auto query = AJson::toString({
      {
        "messages", aui::to_json(messages),
      },
      { "stream", false },
      { "use_context", false },
      { "include_sources", true },
      { "model", model },
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
}

AFuture<std::valarray<float>> OpenAIChat::embedding(AString input, AStringView embeddingModel) {
    auto response = AJson::fromBuffer((co_await ACurl::Builder(baseUrl + "v1/embeddings")
      .withMethod(ACurl::Method::HTTP_POST)
      .withTimeout(4h)
      .withHeaders({"Content-Type: application/json"})
      .withBody(AJson::toString(AJson::Object{
          {"model", embeddingModel},
          {"input", std::move(input)},
      })).runAsync()).body);
    const auto& array = response["data"][0]["embedding"].asArray();

    std::valarray result(0.f, array.size());
    for (const auto&[i, v] : array | ranges::view::enumerate) {
        result[i] = v.asNumber();
    }
    co_return result;
}
