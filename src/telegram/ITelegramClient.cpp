#include "ITelegramClient.h"

template<typename TgObject = td::td_api::chat>
static AFuture<_<TgObject>> genericCacheImpl(
    ITelegramClient& telegram,
    AMap<int64_t, _<ITelegramClient::Cached<TgObject>>>& cache,
    int64_t id,
    aui::factory<td::td_api::object_ptr<td::td_api::Function>> auto queryFunction) {
    if (auto v = cache.contains(id)) {
        co_await v->second->populated;
        co_return AUI_PTR_ALIAS(v->second, tg);
    }
    auto dst = _new<ITelegramClient::Cached<TgObject>>();
    cache[id] = dst;
    dst->populated = [queryFunction = std::move(queryFunction)](ITelegramClient& telegram, ITelegramClient::Cached<TgObject>& dst, int64_t id) -> AFuture<> {
        auto result = co_await telegram.sendQueryWithResult(std::invoke(std::move(queryFunction)));
        dst.tg = std::move(*result);
        co_return;
    }(telegram, *dst, id);

    co_await dst->populated;

    co_return AUI_PTR_ALIAS(dst, tg);
}

AFuture<_<td::td_api::chat>> ITelegramClient::getChat(int64_t id) {
    return genericCacheImpl(*this, mChatCache, id, [=] {
        return td::td_api::make_object<td::td_api::getChat>(id);
    });
}

AFuture<_<td::td_api::user>> ITelegramClient::getUser(int64_t id) {
    return genericCacheImpl(*this, mUserCache, id, [=] {
        return td::td_api::make_object<td::td_api::getUser>(id);
    });
}

AFuture<_<td::td_api::message>> ITelegramClient::getMessage(int64_t chatId, int64_t messageId) {
    return genericCacheImpl(*this, mMessageCache[chatId], messageId, [=] {
        return td::td_api::make_object<td::td_api::getMessage>(chatId, messageId);
    });
}

