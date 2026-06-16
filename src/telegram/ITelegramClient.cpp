#include "ITelegramClient.h"

template<typename QueryFunction = td::td_api::getChat, typename TgObject = td::td_api::chat>
static AFuture<_<TgObject>> genericCacheImpl(ITelegramClient& telegram, AMap<int64_t, _<ITelegramClient::Cached<TgObject>>>& cache, int64_t id) {
    if (auto v = cache.contains(id)) {
        co_await v->second->populated;
        co_return AUI_PTR_ALIAS(v->second, tg);
    }
    auto dst = _new<ITelegramClient::Cached<TgObject>>();
    cache[id] = dst;
    dst->populated = [](ITelegramClient& telegram, ITelegramClient::Cached<TgObject>& dst, int64_t id) -> AFuture<> {
        auto result = co_await telegram.sendQueryWithResult(td::td_api::make_object<QueryFunction>(id));
        dst.tg = std::move(*result);
        co_return;
    }(telegram, *dst, id);

    co_await dst->populated;

    co_return AUI_PTR_ALIAS(dst, tg);
}

AFuture<_<td::td_api::chat>> ITelegramClient::getChat(int64_t id) {
    return genericCacheImpl<td::td_api::getChat>(*this, mChatCache, id);
}

AFuture<_<td::td_api::user>> ITelegramClient::getUser(int64_t id) {
    return genericCacheImpl<td::td_api::getUser>(*this, mUserCache, id);
}
