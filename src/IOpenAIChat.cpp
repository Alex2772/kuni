//
// Created by alex2772 on 5/9/26.
//

#include "IOpenAIChat.h"

#include "AUI/Util/ARandom.h"

#include <range/v3/view/zip.hpp>

AJson AJsonConv<IOpenAIChat::Session>::toJson(const IOpenAIChat::Session& v) {
    AJson::Array result;
    for (const auto& message: v) {
        // reverse engineered from vscode copilot plugin
        if (message.content.contains("</{}>"_format(IOpenAIChat::EMBEDDING_TAG))) {
            auto content = std::string_view(message.content);
            auto append = [&](const IOpenAIChat::Message& msg) {
                if (msg.content.empty()) {
                    return;
                }
                result << aui::to_json(msg);
            };
            for (;;) {
                auto tagPos = content.find("<{}>"_format(IOpenAIChat::EMBEDDING_TAG));
                append(IOpenAIChat::Message{
                    .role = message.role,
                    .content = content.substr(0, tagPos),
                });
                if (tagPos == std::string::npos) {
                    break;
                }
                content = content.substr(tagPos);
                content = content.substr(content.find(">") + 1);
                auto body = content.substr(0, content.find("</{}>"_format(IOpenAIChat::EMBEDDING_TAG)));
                append(IOpenAIChat::Message{
                    .role = message.role,
                    .content = "<attachments>",
                });
                result << AJson::Object{
                    {"role", aui::to_json(message.role)},
                    {"content",
                     AJson::Array{
                         AJson::Object{{"type", "image_url"}, {"image_url", body}},
                     }},
                };
                append(IOpenAIChat::Message{
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

void AJsonConv<IOpenAIChat::Session, void>::fromJson(AJson json, IOpenAIChat::Session& dst) {
    dst.resize(json.asArray().size());
    for (const auto&[in, out] : ranges::views::zip(json.asArray(), dst)) {
        // for better compat
        if (in["tool_calls"].isArray()) {
            aui::from_json(in["tool_calls"], out.tool_calls);
        }
        out.reasoning = in["reasoning"].asStringOpt().valueOr("");
        out.reasoning_content  = in["reasoning_content"].asStringOpt().valueOr("");
        out.content  = in["content"].asStringOpt().valueOr("");
    }
}

AString IOpenAIChat::Session::nextSessionId() {
    static ARandom r;
    return r.nextUuid().toString();
}

AFuture<IOpenAIChat::Response> IOpenAIChat::chat(Params params, IOpenAIChat::Session messages) {
    auto streaming = chatStreaming(std::move(params), std::move(messages));
    co_await streaming->completed;
    co_return *streaming->response;
}

void AJsonConv<IOpenAIChat::Response::Usage, void>::fromJson(const AJson& v, IOpenAIChat::Response::Usage& dst) {
    aui::zero(dst);
    dst.prompt_tokens = v["prompt_tokens"].asLongIntOpt().valueOr(0);
    dst.completion_tokens = v["completion_tokens"].asLongIntOpt().valueOr(0);
    dst.total_tokens = v["total_tokens"].asLongIntOpt().valueOr(0);
    dst.prompt_cache_hit_tokens = v["prompt_cache_hit_tokens"].asLongIntOpt()
        .valueOr([&]() -> int64_t {
            auto promptTokenDetails = v["prompt_tokens_details"].asObjectOpt();
            if (!promptTokenDetails) {
                return -1;
            }
            return (*promptTokenDetails)["cached_tokens"].asLongIntOpt().valueOr(-1); // openrouter
        });
    if (dst.prompt_cache_hit_tokens < 0) {
        dst.prompt_cache_hit_tokens = 0;
    } else {
        dst.prompt_cache_miss_tokens = v["prompt_cache_miss_tokens"].asLongIntOpt() // deepseek
            .valueOr(dst.prompt_tokens - dst.prompt_cache_hit_tokens); // openrouter
    }
}

AJson AJsonConv<IOpenAIChat::Response::Usage, void>::toJson(const IOpenAIChat::Response::Usage& from) {
    AJson dst;
    dst["prompt_tokens"] = from.prompt_tokens;
    dst["completion_tokens"] = from.completion_tokens;
    dst["total_tokens"] = from.total_tokens;
    if (from.prompt_cache_hit_tokens > 0 || from.prompt_cache_miss_tokens > 0) {
        dst["prompt_cache_miss_tokens"] = from.prompt_cache_miss_tokens;
        dst["prompt_cache_hit_tokens"] = from.prompt_cache_hit_tokens;
    }
    return dst;
}
