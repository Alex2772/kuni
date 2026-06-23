#pragma once
#include "Diary.h"
#include "IChatHistoryMessageProcessor.h"
#include "IOpenAIChat.h"
#include "AUI/Common/ATimer.h"
#include "AUI/Util/AYieldGenerator.h"

namespace proxy_server {

/**
 * @brief Bridges proxy session(s) against Kuni's diary and Kuni's temporary context (located in AppBase).
 * @details
 * Dumps session details to the diary, once the session is no longer active after some period of time or papik has
 * written to Kuni (handled by implementing IChatHistoryMessageProcessor interface).
 *
 * The context is summarized by appending a USER message to the end with `config::DIARY_PROMPT` prompt and sending a
 * request to the `endpoint`.
 */
struct ContextBridge : AObject, IChatHistoryMessageProcessor {
    struct Config {
        /**
         * @brief OpenAI endpoint to make summarization with.
         */
        Endpoint endpoint;

        /**
         * @brief Diary to save data to.
         */
        _<Diary> diary;

        /**
         * @brief Lifetime of a session. On timeout, the session is dropped to the diary automatically.
         */
        std::chrono::seconds timeout = std::chrono::hours(1);
    };

    ContextBridge(Config config);
    ~ContextBridge() override = default;
    AFuture<AString>
    processChatHistoryMessage(const td::td_api::chat& chat, const td::td_api::message& msg, AString formatted) override;

    AFuture<AVector<AString /* */>> collectAndSaveSessionsNotNewerThan(std::chrono::system_clock::time_point tp);

    /**
     * @brief Called by the proxy session when it received a new bunch of messages.
     * @param request request JSON object, structurally complying with `OpenAIChatImpl::makeQueryString`.
     * @details
     * Handles proxy_server::IProxyServer::sentRequestToLLM signal.
     *
     * Identifies session by the request["messages"] JSON array.
     *
     * We use AJson instead of i.e., IOpenAIChat::Session in order to preserve client's original arguments
     * (like top_k) and avoid expensive cache misses.
     */
    void collectRequestToLLM(AJson request);

    AFuture<> update();

private:
    Config mConfig;
    _<ATimer> mUpdateTimer;
    AAsyncHolder mAsyncHolder;
    struct PendingSession {
        AString salt;
        AJson request;
        std::chrono::system_clock::time_point updateTime;
    };
    AVector<PendingSession> mPendingSessions;
};
}   // namespace proxy_server