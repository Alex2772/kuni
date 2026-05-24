#pragma once
#include "AUI/Common/AString.h"
#include "AUI/Common/AVector.h"
#include "AUI/Json/AJson.h"
#include "IOpenAIChat.h"
#include "MetricsBreadcumbs.h"

struct OpenAITools {
    struct Ctx {
        OpenAITools& tools;
        AJson args;
        const AVector<IOpenAIChat::Message::ToolCall>& allToolCalls;
    };
    using Handler = std::function<AFuture<AString>(Ctx ctx)>;

    struct Tool {
        AString type = "function";
        AString name;
        AString description;
        struct Parameters {
            AString type = "object";
            struct Property {
                AString type = "string";
                AString description;
            };
            AMap<AString, Property> properties;
            AVector<AString> required; // required properties
            bool additionalProperties = false;
        } parameters;
        bool strict = true;
        Handler handler;
    };

    OpenAITools(std::initializer_list<Tool> tools);

    /**
     * @brief Optional hook fired after each tool call handler completes successfully.
     * Not called if the handler throws. Set by AppBase::updateTools to emit AppBase::toolCallFired.
     */
    std::function<void(const AString& toolName)> onAfterToolCall;

    AFuture<AVector<IOpenAIChat::Message>> handleToolCalls(const AVector<IOpenAIChat::Message::ToolCall>& toolCalls, const _<MetricsBreadcumbs>& metricsBreadCumbs = nullptr);

    AJson asJson() const;

    [[nodiscard]] AMap<AString, Tool> handlers() const { return mHandlers; }

    void insert(Tool tool) { mHandlers[tool.name] = std::move(tool); }

private:
    AMap<AString, Tool> mHandlers;
};
