#pragma once
#include "AUI/Common/AObject.h"

#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

#include "AUI/Common/AMap.h"
#include "AUI/Common/ASignal.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AFuture.h"

/**
 * @brief Abstract interface for Telegram client operations.
 */
struct ITelegramClient {
    virtual ~ITelegramClient() = default;

    using Object = td::td_api::object_ptr<td::td_api::Object>;

    virtual AFuture<Object> sendQuery(td::td_api::object_ptr<td::td_api::Function> f) = 0;

    template<aui::derived_from<td::td_api::Function> F>
    AFuture<td::td_api::object_ptr<typename F::ReturnType::element_type>> sendQueryWithResult(td::td_api::object_ptr<F> f) {
        auto object = co_await sendQuery(std::move(f));
        if (object->get_id() == td::td_api::error::ID) {
            auto error = td::move_tl_object_as<td::td_api::error>(std::move(object));
            if (AStringView(error->message_).contains("FROZEN_METHOD_INVALID")) {
                // this error occurs when the userbot was reported by schoolkids and therefore suspended (banned).
                // we'll crash the application immediately to prevent failed API calls.
                // get a tg premium subscription to minimize risks of doxx.о
                ALogger::err("ITelegramClient") << "The userbot's account was suspended - can't operate";
                std::terminate();
            }
            throw AException(error->message_);
        }
        if constexpr (requires { F::ReturnType::element_type::ID; }) {
            AUI_ASSERT(object->get_id() == F::ReturnType::element_type::ID);
        }
        co_return td::move_tl_object_as<typename F::ReturnType::element_type>(std::move(object));
    }

    template<typename TgObject>
    struct Cached {
        TgObject tg;
        AFuture<> populated;
    };

    [[nodiscard]]
    AFuture<_<td::td_api::chat>> getChat(int64_t id);

    [[nodiscard]]
    AFuture<_<td::td_api::user>> getUser(int64_t id);

    [[nodiscard]]
    AFuture<_<td::td_api::message>> getMessage(int64_t chatId, int64_t messageId);

    [[nodiscard]]
    virtual const AFuture<>& waitForConnection() const noexcept = 0;

    [[nodiscard]] virtual int64_t myId() const = 0;

    std::function<void(Object)> onEvent = [](Object o) {};

    template<typename T>
    static td::td_api::object_ptr<T> toPtr(T&& t) {
        return td::td_api::make_object<T>(std::forward<T>(t));
    }

    enum class ConnectionState {
        INITIALIZING,
        CONNECTING,
        CONNECTING_TO_PROXY,
        UPDATING,
        WAITING_FOR_NETWORK,
        CONNECTED,
    } connectionState = ConnectionState::INITIALIZING;

    emits<> loggedIn;


protected:
    template<typename TgObject>
    using Cache = AMap<int64_t /* id */, _<Cached<TgObject>>>;
    Cache<td::td_api::chat> mChatCache;
    Cache<td::td_api::user> mUserCache;
    AMap<int64_t /* chatId */, Cache<td::td_api::message>> mMessageCache;
};
