//
// Created by alex2772 on 3/2/26.
//

#include "TelegramClient.h"

#include "AUI/Common/ATimer.h"
#include "AUI/Util/kAUI.h"
#include "util/secrets.h"

using namespace std::chrono_literals;

namespace {
    static constexpr auto LOG_TAG = "TelegramClient";
} // namespace


TelegramClient::TelegramClient() : mTgUpdateTimer(_new<ATimer>(1s)) {
    ALOG_TRACE(LOG_TAG) << "TelegramClient::TelegramClient";
    setSlotsCallsOnlyOnMyThread(true);

    td::ClientManager::execute(td::td_api::make_object<td::td_api::setLogVerbosityLevel>(1));
    initClientManager();

    AObject::connect(mTgUpdateTimer->fired, me::update);
    mTgUpdateTimer->start();
}

AFuture<TelegramClient::Object> TelegramClient::sendQuery(td::td_api::object_ptr<td::td_api::Function> f) {
    ALOG_TRACE(LOG_TAG) << "sendQuery";
    if (mQueryCountLastUpdate++ >= 5) {
        // Telegram is strict about using 3rdparty telegram clients. For this reason, we have to ensure that we wouldn't
        // trigger their security leading to ban of the account.
        //
        // If the application starts to SPAM with queries, we simply crash it.
        co_await AThread::asyncSleep(1s);
    }

    auto query_id = ++mCurrentQueryId;
    AFuture<TelegramClient::Object> result;
    mHandlers.emplace(query_id, [result](Object object) { result.supplyValue(std::move(object)); });
    mClientManager->send(mClientId, query_id, std::move(f));
    co_return co_await result;
}

void TelegramClient::initClientManager() {
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

void TelegramClient::update() {
    ALOG_TRACE(LOG_TAG) << "update";
    mQueryCountLastUpdate = 0;
    for (;;) {
        auto response = mClientManager->receive(0);
        if (!response.object) {
            return;
        }
        processResponse(std::move(response));
    }
}

void TelegramClient::processResponse(td::ClientManager::Response response) {
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

void TelegramClient::commonHandler(td::tl::unique_ptr<td::td_api::Object> object) {
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
                        StubHandler{},
                    });
            },

            [this](td::td_api::updateConnectionState& u) {
                td::td_api::downcast_call(
                    *u.state_, aui::lambda_overloaded{
                                   [&](td::td_api::connectionStateReady&) { mWaitForConnection.supplyValue(); },
                                   [&](auto&&) {},
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
            [&](auto& i) {
                onEvent(std::move(object));
            }});
}
