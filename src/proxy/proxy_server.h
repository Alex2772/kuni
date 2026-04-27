#pragma once
#include "AUI/Thread/AFuture.h"

namespace proxy_server {
struct ProxyServer {
    virtual ~ProxyServer() = default;
    virtual void stop() = 0;
};
_<ProxyServer> init();
}