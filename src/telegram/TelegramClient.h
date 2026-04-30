#pragma once
#include "AUI/Common/AObject.h"

#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

#include "AUI/Common/AMap.h"
#include "AUI/Common/ASignal.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AFuture.h"


class ATimer;
class TelegramClient: public AObject{
public:
    struct StubHandler {
        void operator()(auto& v) const { ALOG_TRACE("TelegramClient") << "Stub: " << to_string(v); }
    };
    TelegramClient();

    using Object = td::td_api::object_ptr<td::td_api::Object>;

    AFuture<Object> sendQuery(td::td_api::object_ptr<td::td_api::Function> f);

    template<aui::derived_from<td::td_api::Function> F>
    AFuture<td::td_api::object_ptr<typename F::ReturnType::element_type>> sendQueryWithResult(td::td_api::object_ptr<F> f) {
        auto object = co_await sendQuery(std::move(f));
        if (object->get_id() == td::td_api::error::ID) {
            auto error = td::move_tl_object_as<td::td_api::error>(std::move(object));
            throw AException(error->message_);
        }
        co_return td::move_tl_object_as<typename F::ReturnType::element_type>(std::move(object));
    }

    [[nodiscard]]
    const AFuture<>& waitForConnection() const noexcept {
        return mWaitForConnection;
    }

    [[nodiscard]] int64_t myId() const { return mMyId; }

    std::function<void(Object)> onEvent = [](Object o) { StubHandler{}(o); };

    template<typename T>
    static td::td_api::object_ptr<T> toPtr(T&& t) {
        return td::td_api::make_object<T>(std::forward<T>(t));
    }

    emits<> loggedIn;

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