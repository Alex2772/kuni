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
            throw AException(error->message_);
        }
        if constexpr (requires { F::ReturnType::element_type::ID; }) {
            AUI_ASSERT(object->get_id() == F::ReturnType::element_type::ID);
        }
        co_return td::move_tl_object_as<typename F::ReturnType::element_type>(std::move(object));
    }

    [[nodiscard]]
    virtual const AFuture<>& waitForConnection() const noexcept = 0;

    [[nodiscard]] virtual int64_t myId() const = 0;

    std::function<void(Object)> onEvent = [](Object o) {};

    template<typename T>
    static td::td_api::object_ptr<T> toPtr(T&& t) {
        return td::td_api::make_object<T>(std::forward<T>(t));
    }

    emits<> loggedIn;
};
