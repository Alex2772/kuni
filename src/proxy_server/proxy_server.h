#pragma once
#include "IOpenAIChat.h"
#include "OpenAITools.h"

#include <memory>
#include <functional>

namespace proxy_server {

using ToolFactory = std::function<OpenAITools(const AVector<IOpenAIChat::Message>&)>;

class IProxyServer {
public:
    virtual ~IProxyServer() = default;
};
std::shared_ptr<IProxyServer> init(std::function<OpenAITools(const AVector<IOpenAIChat::Message>&)> toolsFactory);
}   // namespace proxy_server