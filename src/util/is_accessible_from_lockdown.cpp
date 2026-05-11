#include "is_accessible_from_lockdown.h"

#include "config.h"

#include <range/v3/algorithm/contains.hpp>

AFuture<bool> util::isAccessibleFromLockdown(ITelegramClient& telegram, int64_t chatId) {
#ifdef AUI_TESTS_MODULE
    co_return config::PAPIK_CHAT_ID == chatId;
#else
    switch (config::LOCKDOWN_MODE) {
        case config::LockdownMode::NONE: {
            co_return true;
        }

        case config::LockdownMode::CONTACTS_ONLY: {
            static auto contacts = co_await telegram.sendQueryWithResult(ITelegramClient::toPtr(td::td_api::getContacts()));
            if (ranges::contains(contacts->user_ids_, chatId)) {
                co_return true;
            }
            [[fallthrough]];
        }

        case config::LockdownMode::PAPIK_ONLY: {
            if (chatId == config::PAPIK_CHAT_ID) {
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