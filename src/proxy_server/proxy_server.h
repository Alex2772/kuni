#pragma once
#include "IOpenAIChat.h"
#include "OpenAITools.h"
#include "Endpoint.h"

#include <memory>
#include <functional>

namespace proxy_server {

using ToolFactory = std::function<OpenAITools(const AVector<IOpenAIChat::Message>&)>;

struct Config {
    Endpoint upstreamEndpoint;
    int port = 10434;
};

class IProxyServer {
public:
    virtual ~IProxyServer() = default;
    virtual void waitUntilReady() = 0;
};

std::shared_ptr<IProxyServer> init(ToolFactory toolsFactory, Config config = {});

}   // namespace proxy_server