#include "is_accessible_from_lockdown.h"

#include "config.h"

#include <range/v3/algorithm/contains.hpp>

AFuture<bool> util::isAccessibleFromLockdown(ITelegramClient& telegram, int64_t chatId) {
#ifdef AUI_TESTS_MODULE
    co_return config().papikChatId == chatId;
#else
    switch (config().lockdown) {
        case Config::LockdownMode::NONE: {
            co_return true;
        }

        case Config::LockdownMode::CONTACTS_ONLY: {
            if (chatId < 0) {
                // a group chat that kuni participate, ok.
                co_return true;
            }
            static auto contacts = co_await telegram.sendQueryWithResult(ITelegramClient::toPtr(td::td_api::getContacts()));
            if (ranges::contains(contacts->user_ids_, chatId)) {
                co_return true;
            }
            [[fallthrough]];
        }

        case Config::LockdownMode::PAPIK_ONLY: {
            if (chatId == config().papikChatId) {
                co_return true;
            }
            [[fallthrough]];
        }

        default: {
            co_return false;
        }
    }
#endif
}