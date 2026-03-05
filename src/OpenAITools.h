#pragma once
#include "AUI/Common/AString.h"
#include "AUI/Common/AVector.h"
#include "AUI/Json/AJson.h"
#include "OpenAIChat.h"


struct OpenAITools {
    struct Ctx {
        OpenAITools& tools;
        AJson args;
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

    AFuture<AVector<OpenAIChat::Message>> handleToolCalls(const AVector<OpenAIChat::Message::ToolCall>& toolCalls);

    AJson asJson() const;

    [[nodiscard]] AMap<AString, Tool> handlers() const { return mHandlers; }

    void insert(Tool tool) { mHandlers[tool.name] = std::move(tool); }

private:
    AMap<AString, Tool> mHandlers;
};
