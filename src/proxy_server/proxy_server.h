#pragma once
#include <memory>

namespace proxy_server {
class IProxyServer {
public:
    virtual ~IProxyServer() = default;
};
std::shared_ptr<IProxyServer> init();
}   // namespace proxy_server