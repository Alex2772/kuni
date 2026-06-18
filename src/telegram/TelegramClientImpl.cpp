//
// Created by alex2772 on 3/2/26.
//

#include "TelegramClientImpl.h"

#include "AUI/Common/ATimer.h"
#include "AUI/Util/kAUI.h"
#include "util/secrets.h"

using namespace std::chrono_literals;

namespace {
    static constexpr auto LOG_TAG = "TelegramClient";
} // namespace


TelegramClientImpl::TelegramClientImpl() : mTgUpdateTimer(_new<ATimer>(1s)) {
    ALOG_TRACE(LOG_TAG) << "TelegramClientImpl::TelegramClientImpl";
    setSlotsCallsOnlyOnMyThread(true);

    td::ClientManager::execute(td::td_api::make_object<td::td_api::setLogVerbosityLevel>(1));
    initClientManager();

    AObject::connect(mTgUpdateTimer->fired, me::update);
    mTgUpdateTimer->start();
}

AFuture<ITelegramClient::Object> TelegramClientImpl::sendQuery(td::td_api::object_ptr<td::td_api::Function> f) {
    ALOG_TRACE(LOG_TAG) << "sendQuery " << td::td_api::to_string(f);
    if (mQueryCountLastUpdate++ >= 5) {
        // Telegram is strict about using 3rdparty telegram clients. For this reason, we have to ensure that we wouldn't
        // trigger their security leading to ban of the account.
        ALogger::info(LOG_TAG) << "Too many calls to tdlib! Throttling...\n" << AStacktrace::capture(1, 8);
        co_await AThread::asyncSleep(1s);
    }

    auto query_id = ++mCurrentQueryId;
    AFuture<ITelegramClient::Object> result;
    mHandlers.emplace(query_id, [result](Object object) { result.supplyValue(std::move(object)); });
    mClientManager->send(mClientId, query_id, std::move(f));
    co_return co_await result;
}

void TelegramClientImpl::initClientManager() {
    ALOG_TRACE(LOG_TAG) << "initClientManager";
    mClientManager = std::make_unique<td::ClientManager>();
    mClientId = mClientManager->create_client_id();
    sendQueryWithResult(td::td_api::make_object<td::td_api::getOption>("version"))
        .onSuccess([](const td::td_api::object_ptr<td::td_api::OptionValue>& object) {
            td::td_api::downcast_call(*const_cast<td::td_api::object_ptr<td::td_api::OptionValue>&>(object),
                                      aui::lambda_overloaded{[](const td::td_api::optionValueString& u) {
                                                                 ALogger::info(LOG_TAG)
                                                                     << "Tdlib version: " << u.value_;
                                                             },
                                                             [](auto&) {}});
        });
}

void TelegramClientImpl::update() {
    ALOG_TRACE(LOG_TAG) << "update";

    switch (connectionState) {
        case ConnectionState::INITIALIZING:
            ALogger::info(LOG_TAG) << "Connection state: initializing...";
            break;
        case ConnectionState::CONNECTED:
            break;
        case ConnectionState::CONNECTING:
            ALogger::info(LOG_TAG) << "Connection state: connecting... (check VPN/proxy settings)";
            break;
        case ConnectionState::CONNECTING_TO_PROXY:
            ALogger::info(LOG_TAG) << "Connection state: connecting to proxy...";
            break;
        case ConnectionState::UPDATING:
            ALogger::info(LOG_TAG) << "Connection state: updating...";
            break;
        case ConnectionState::WAITING_FOR_NETWORK:
            ALogger::info(LOG_TAG) << "Connection state: waiting for network...";
            break;
    }

    mQueryCountLastUpdate = 0;
    for (;;) {
        auto response = mClientManager->receive(0);
        if (!response.object) {
            return;
        }
        processResponse(std::move(response));
    }
}

void TelegramClientImpl::processResponse(td::ClientManager::Response response) {
    ALOG_TRACE(LOG_TAG) << "processResponse";
    if (!response.object) {
        return;
    }

    if (auto c = mHandlers.contains(response.request_id)) {
        auto handler = std::move(c->second);
        mHandlers.erase(*c);
        handler(std::move(response.object));
        return;
    }

    commonHandler(std::move(response.object));
}

void TelegramClientImpl::commonHandler(td::tl::unique_ptr<td::td_api::Object> object) {
    ALOG_TRACE(LOG_TAG) << "commonHandler";
    td::td_api::downcast_call(
        *object,
        aui::lambda_overloaded{
            [this](td::td_api::updateAuthorizationState& update_authorization_state) {
                td::td_api::downcast_call(
                    *update_authorization_state.authorization_state_,
                    aui::lambda_overloaded{
                        [this](td::td_api::authorizationStateWaitTdlibParameters& u) {
                            auto parameters = td::td_api::make_object<td::td_api::setTdlibParameters>();
                            parameters->database_directory_ = "tdlib";
                            parameters->use_message_database_ = true;
                            parameters->use_secret_chats_ = true;

                            parameters->api_id_ = util::secrets()["telegram_api"]["id"].as_integer();
                            parameters->api_hash_ = util::secrets()["telegram_api"]["hash"].as_string();
                            parameters->system_language_code_ = "en";
                            parameters->device_model_ = "Desktop";
                            parameters->application_version_ = AUI_PP_STRINGIZE(AUI_CMAKE_PROJECT_VERSION);
                            sendQuery(std::move(parameters));
                        },
                        [this](td::td_api::authorizationStateReady& u) {
                            ALogger::info(LOG_TAG) << "[Authentication] logged in.";
                            emit loggedIn;
                        },
                        [this](td::td_api::authorizationStateWaitPhoneNumber& s) {
                            ALogger::info(LOG_TAG) << "[Authentication] required. Please supply phone number to stdin";

                            auto params = td::td_api::make_object<td::td_api::setAuthenticationPhoneNumber>();
                            std::cin >> params->phone_number_;
                            sendQuery(std::move(params));
                        },
                        [this](td::td_api::authorizationStateWaitPassword& s) {
                            ALogger::info(LOG_TAG) << "[Authentication] required. Please supply cloud "
                                                      "password to stdin";

                            auto params = td::td_api::make_object<td::td_api::checkAuthenticationPassword>();
                            std::cin >> params->password_;
                            sendQuery(std::move(params));
                        },
                        [this](td::td_api::authorizationStateWaitCode& s) {
                            ALogger::info(LOG_TAG) << "[Authentication] required. Please supply "
                                                      "verification code to stdin";

                            auto params = td::td_api::make_object<td::td_api::checkAuthenticationCode>();
                            std::cin >> params->code_;
                            sendQuery(std::move(params));
                        },
                        [this](td::td_api::authorizationStateClosed& u) {
                            getThread()->enqueue([this, self = shared_from_this()] { initClientManager(); });
                        },
                        [this](auto& v) {
                            ALogger::info(LOG_TAG) << "Stub: " << td::td_api::to_string(v);
                        },
                    });
            },

            [this](td::td_api::updateConnectionState& u) {
              td::td_api::downcast_call(
                  *u.state_,
                  aui::lambda_overloaded {
                    [&](td::td_api::connectionStateReady&) {
                        connectionState = ConnectionState::CONNECTED;
                        ALogger::info(LOG_TAG) << "Connection state: connected";
                        mWaitForConnection.supplyValue();
                    },
                    [&](td::td_api::connectionStateConnecting&) { connectionState = ConnectionState::CONNECTING; },
                    [&](td::td_api::connectionStateConnectingToProxy&) {
                        connectionState = ConnectionState::CONNECTING_TO_PROXY;
                    },
                    [&](td::td_api::connectionStateWaitingForNetwork&) {
                        connectionState = ConnectionState::WAITING_FOR_NETWORK;
                    },

                    [&](td::td_api::connectionStateUpdating&) { connectionState = ConnectionState::UPDATING; },
            });
            },
            [this](td::td_api::updateOption& u) {
                if (u.name_ == "my_id") {
                    td::td_api::downcast_call(*u.value_, aui::lambda_overloaded{
                        [&](td::td_api::optionValueInteger& i) {
                            mMyId = i.value_;
                        },
                        [&](auto&) {},
                    });
                }
            },
            // ── User cache updates ──────────────────────────────────────────
            [this](td::td_api::updateUser& u) {
                if (auto dst = mUserCache.contains(u.user_->id_)) {
                    dst->second->tg = std::move(*u.user_);
                }
            },
            [this](td::td_api::updateUserStatus& u) {
                if (auto dst = mUserCache.contains(u.user_id_)) {
                    dst->second->tg.status_ = std::move(u.status_);
                }
            },
            // ── Chat cache updates ───────────────────────────────────────────
            [this](td::td_api::updateChatTitle& u) {
                if (auto dst = mChatCache.contains(u.chat_id_)) {
                    dst->second->tg.title_ = std::move(u.title_);
                }
            },
            [this](td::td_api::updateChatPhoto& u) {
                if (auto dst = mChatCache.contains(u.chat_id_)) {
                    dst->second->tg.photo_ = std::move(u.photo_);
                }
            },
            [this](td::td_api::updateChatPermissions& u) {
                if (auto dst = mChatCache.contains(u.chat_id_)) {
                    dst->second->tg.permissions_ = std::move(u.permissions_);
                }
            },
            [this](td::td_api::updateChatLastMessage& u) {
                if (auto dst = mChatCache.contains(u.chat_id_)) {
                    dst->second->tg.last_message_ = std::move(u.last_message_);
                }
            },
            [this](td::td_api::updateChatReadInbox& u) {
                if (auto dst = mChatCache.contains(u.chat_id_)) {
                    dst->second->tg.last_read_inbox_message_id_ = u.last_read_inbox_message_id_;
                    dst->second->tg.unread_count_ = u.unread_count_;
                }
            },
            [this](td::td_api::updateChatReadOutbox& u) {
                if (auto dst = mChatCache.contains(u.chat_id_)) {
                    dst->second->tg.last_read_outbox_message_id_ = u.last_read_outbox_message_id_;
                }
            },
            [this](td::td_api::updateChatUnreadMentionCount& u) {
                if (auto dst = mChatCache.contains(u.chat_id_)) {
                    dst->second->tg.unread_mention_count_ = u.unread_mention_count_;
                }
            },
            [this](td::td_api::updateChatUnreadReactionCount& u) {
                if (auto dst = mChatCache.contains(u.chat_id_)) {
                    dst->second->tg.unread_reaction_count_ = u.unread_reaction_count_;
                }
            },
            [this](td::td_api::updateChatNotificationSettings& u) {
                if (auto dst = mChatCache.contains(u.chat_id_)) {
                    dst->second->tg.notification_settings_ = std::move(u.notification_settings_);
                }
            },
            [this](td::td_api::updateChatIsMarkedAsUnread& u) {
                if (auto dst = mChatCache.contains(u.chat_id_)) {
                    dst->second->tg.is_marked_as_unread_ = u.is_marked_as_unread_;
                }
            },
            [this](td::td_api::updateChatBlockList& u) {
                if (auto dst = mChatCache.contains(u.chat_id_)) {
                    dst->second->tg.block_list_ = std::move(u.block_list_);
                }
            },
            [this](td::td_api::updateChatHasScheduledMessages& u) {
                if (auto dst = mChatCache.contains(u.chat_id_)) {
                    dst->second->tg.has_scheduled_messages_ = u.has_scheduled_messages_;
                }
            },
            [this](td::td_api::updateChatDraftMessage& u) {
                if (auto dst = mChatCache.contains(u.chat_id_)) {
                    dst->second->tg.draft_message_ = std::move(u.draft_message_);
                }
            },
            [this](td::td_api::updateChatMessageAutoDeleteTime& u) {
                if (auto dst = mChatCache.contains(u.chat_id_)) {
                    dst->second->tg.message_auto_delete_time_ = u.message_auto_delete_time_;
                }
            },
            [this](td::td_api::updateChatEmojiStatus& u) {
                if (auto dst = mChatCache.contains(u.chat_id_)) {
                    dst->second->tg.emoji_status_ = std::move(u.emoji_status_);
                }
            },
            [this](td::td_api::updateChatBackground& u) {
                if (auto dst = mChatCache.contains(u.chat_id_)) {
                    dst->second->tg.background_ = std::move(u.background_);
                }
            },
            [this](td::td_api::updateChatTheme& u) {
                if (auto dst = mChatCache.contains(u.chat_id_)) {
                    dst->second->tg.theme_ = std::move(u.theme_);
                }
            },
            [this](td::td_api::updateChatReplyMarkup& u) {
                if (auto dst = mChatCache.contains(u.chat_id_)) {
                    dst->second->tg.reply_markup_message_id_ = u.reply_markup_message_->id_;
                }
            },
            [this](td::td_api::updateMessageSendSucceeded& u) {
                const auto oldId = u.old_message_id_;
                const auto newId  = u.message_->id_;
                auto& cacheForThisChat = mMessageCache[u.message_->chat_id_];
                auto dst = cacheForThisChat[newId] = cacheForThisChat[oldId];
                if (dst == nullptr) {
                    dst = cacheForThisChat[newId] = cacheForThisChat[oldId] = _new<Cached<td::td_api::message>>();
                }
                dst->tg = std::move(*u.message_);
                if (!dst->populated.hasResult()) {
                    dst->populated.supplyValue();
                }
            },
            [this](td::td_api::updateMessageSendFailed& u) {
                const auto oldId = u.old_message_id_;
                const auto newId  = u.message_->id_;
                auto& cacheForThisChat = mMessageCache[u.message_->chat_id_];
                auto dst = cacheForThisChat[newId] = cacheForThisChat[oldId];
                if (dst == nullptr) {
                    dst = cacheForThisChat[newId] = cacheForThisChat[oldId] = _new<Cached<td::td_api::message>>();
                }
                dst->tg = std::move(*u.message_);
                if (!dst->populated.hasResult()) {
                    dst->populated.supplyValue();
                }
            },
            [&](auto& i) {
                onEvent(std::move(object));
            }});
}