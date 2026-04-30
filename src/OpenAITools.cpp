//
// Created by alex2772 on 2/27/26.
//

#include "OpenAITools.h"

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/transform.hpp>

AJSON_FIELDS(OpenAITools::Tool::Parameters::Property, AJSON_FIELDS_ENTRY(type) AJSON_FIELDS_ENTRY(description))

AJSON_FIELDS(OpenAITools::Tool::Parameters,
             AJSON_FIELDS_ENTRY(type) AJSON_FIELDS_ENTRY(properties) AJSON_FIELDS_ENTRY(required)
                     AJSON_FIELDS_ENTRY(additionalProperties))

AJSON_FIELDS(OpenAITools::Tool, AJSON_FIELDS_ENTRY(type) AJSON_FIELDS_ENTRY(name) AJSON_FIELDS_ENTRY(description)
                                        AJSON_FIELDS_ENTRY(parameters) AJSON_FIELDS_ENTRY(strict))


OpenAITools::OpenAITools(std::initializer_list<Tool> tools) {
    ALOG_TRACE("OpenAITools") << "OpenAITools";
    for (const auto& tool : tools) {
        AUI_ASSERT(tool.handler != nullptr);
        mHandlers[tool.name] = tool;
    }
}

AFuture<AVector<OpenAIChat::Message>> OpenAITools::handleToolCalls(const AVector<OpenAIChat::Message::ToolCall>& toolCalls) {
    ALOG_TRACE("OpenAITools") << "handleToolCalls";
    AVector<OpenAIChat::Message> result;
    for (const auto& toolCall : toolCalls) {
        result << OpenAIChat::Message{
            .role = OpenAIChat::Message::Role::TOOL,
            .content = co_await [&]() -> AFuture<AString> {
                try {
                    if (auto c = mHandlers.contains(toolCall.function.name)) {
                        co_return co_await c->second.handler({*this, AJson::fromString(toolCall.function.arguments)});
                    }
                    co_return "tool \"" + toolCall.function.name + "\" is not currently available. Please use another tool instead.";
                } catch (const AException& e) {
                    ALogger::err("OpenAITools") << "error while executing \"{}\" tool: "_format(toolCall.function.name) << e;
                    co_return "error while executing \"{}\" tool: {}"_format(toolCall.function.name, e.getMessage());
                }
            }(),
            .tool_call_id = toolCall.id,
        };
    }
    co_return result;

}

AJson OpenAITools::asJson() const {
    ALOG_TRACE("OpenAITools") << "asJson";
    return ranges::view::transform(mHandlers, [](const auto& tool) {
        return AJson::Object{
            {"type", tool.second.type },
            {"function", aui::to_json(tool.second) },
        };
    }) | ranges::to<AJson::Array>();
}
