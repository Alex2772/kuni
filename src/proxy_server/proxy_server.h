#pragma once
#include "IOpenAIChat.h"

#include <memory>

namespace proxy_server {
class IProxyServer {
public:
    virtual ~IProxyServer() = default;
};
std::shared_ptr<IProxyServer> init(_<IOpenAIChat> openAI);
}   // namespace proxy_server