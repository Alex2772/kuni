//
// Created by alex2772 on 5/9/26.
//

#include "IOpenAIChat.h"

AJson AJsonConv<AVector<IOpenAIChat::Message>>::toJson(const AVector<IOpenAIChat::Message>& v) {
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
