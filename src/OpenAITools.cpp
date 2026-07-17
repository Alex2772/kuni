//
// Created by alex2772 on 2/27/26.
//

#include "OpenAITools.h"

#include <range/v3/algorithm/remove_if.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/transform.hpp>

template<>
struct AJsonConv<OpenAITools::Tool::Parameters::Property> {
    using Property = OpenAITools::Tool::Parameters::Property;

    static AJson toJson(const Property& p) {
        AJson::Object json;
        json["description"] = p.description;

        AVector<AString> types = { p.type };
        if (p.orArray) {
            types << "array";
        }
        if (p.nullable) {
            types << "null";
        }
        json["type"] = types.size() == 1 ? AJson(types.first()) : aui::to_json(types);

        if ((p.type == "array" || p.orArray)) {
            AUI_ASSERTX(p.items != nullptr, "array-typed property must specify items");
            json["items"] = aui::to_json(*p.items);
        }
        return std::move(json);
    }

    static void fromJson(const AJson& json, Property& dst) {
        // tool schemas are write-only (sent to the LLM), so parsing back is not needed.
        AUI_ASSERT(0 && "not implemented");
    }
};

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

static AString removeControlCharacters(AString input) {
    // toml11 library inserts beautiful formatting to the exceptions, and JSON is not happy about it.
    input.bytes().erase(ranges::remove_if(input.bytes(), [](char c) {
        switch (c) {
            case '\n':
            case '\t':
                return false;
            default:
            return 0x00 <= c && c < 0x20;
        }
    }), input.bytes().end());
    return input;
}

AFuture<IOpenAIChat::Session> OpenAITools::handleToolCalls(const AVector<IOpenAIChat::Message::ToolCall>& toolCalls,
    const _<MetricsBreadcumbs>& metricsBreadCumbs) {
    ALOG_TRACE("OpenAITools") << "handleToolCalls";
    IOpenAIChat::Session result;
    for (const auto& toolCall : toolCalls) {
        result << IOpenAIChat::Message{
            .role = IOpenAIChat::Message::Role::TOOL,
            .content = removeControlCharacters(co_await [&]() -> AFuture<AString> {
                try {
                    if (auto c = mHandlers.contains(toolCall.function.name)) {
                        AOptional<MetricsBreadcumbs::Point> point;
                        if (metricsBreadCumbs) {
                            point.emplace(metricsBreadCumbs, "function", toolCall.function.name);
                        }
                        auto handlerResult = co_await c->second.handler({
                            .tools = *this,
                            .args = AJson::fromString(toolCall.function.arguments),
                            .allToolCalls = toolCalls,
                        });
                        if (onAfterToolCall) {
                            onAfterToolCall(toolCall.function.name);
                        }
                        co_return std::move(handlerResult);
                    }
                    co_return "tool \"" + toolCall.function.name + "\" is not currently available. Please use another tool instead.";
                } catch (const AException& e) {
                    ALogger::err("OpenAITools") << "error while executing \"{}\" tool: "_format(toolCall.function.name) << e;
                    co_return "error while executing \"{}\" tool: {}"_format(toolCall.function.name, e.getMessage());
                }
            }()),
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
