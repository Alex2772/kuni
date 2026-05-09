#pragma once
#include "ITelegramClient.h"

#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

#include "AUI/Common/AMap.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AFuture.h"


class ATimer;

/**
 * @brief Concrete implementation of ITelegramClient using TDLib.
 */
class TelegramClientImpl: public ITelegramClient, public AObject {
public:
    struct StubHandler {
        void operator()(auto& v) const { ALOG_TRACE("TelegramClient") << "Stub: " << to_string(v); }
    };
    TelegramClientImpl();

    AFuture<Object> sendQuery(td::td_api::object_ptr<td::td_api::Function> f) override;

    [[nodiscard]]
    const AFuture<>& waitForConnection() const noexcept override {
        return mWaitForConnection;
    }

    [[nodiscard]] int64_t myId() const override { return mMyId; }

private:
    AFuture<> mWaitForConnection;
    _<ATimer> mTgUpdateTimer;
    std::unique_ptr<td::ClientManager> mClientManager;
    td::ClientManager::ClientId mClientId{};
    AMap<std::uint64_t, std::function<void(Object)>> mHandlers;
    size_t mQueryCountLastUpdate{};
    size_t mCurrentQueryId{};
    int64_t mMyId{};

    void update();
    void initClientManager();

    void commonHandler(td::tl::unique_ptr<td::td_api::Object> object);
    void processResponse(td::ClientManager::Response response);
};
