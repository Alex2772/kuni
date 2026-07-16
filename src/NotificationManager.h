#pragma once
#include "OpenAITools.h"
#include "AUI/Util/kAUI.h"
#include "AUI/Thread/AFuture.h"

#include <deque>
#include <list>
#include <range/v3/algorithm/find_if.hpp>

class NotificationManager {
public:
    struct Notification {
        /**
         * @brief Notification message in natural language passed to LLM.
         * @details
         * Example: You have received a message from User. Would you like to process it?
         */
        AString message;

        /**
         * @brief Related tools available to the LLM when processing this notification.
         * @details
         * Example: "open" tool to open the chat the notification came from.
         */
        OpenAITools actions;

        /**
         * @brief Priority of the notification. Higher priority notifications will be processed earlier.
         */
        int priority = 0;

        /**
         * @brief Optional freeform pin token that poisons the worker that will process the notification.
         * @details
         * Example pin string: "<chat id=1235 />".
         *
         * The worker pinning mechanism was introduced to pin a chat to specific worker, so the same chat is processed
         * by the same worker, which has more related context.
         */
        AOptional<AString> pin;
    };

    struct NotificationHandle {
        Notification notification;

        /**
         * @brief Resolves when the notification was passed to the worker.
         */
        AFuture<> onStartedProcessing;

        /**
         * @brief Resolved by the worker when the notification pass completely processed.
         */
        AFuture<> onProcessed;
    };

    /**
     * @brief Registers a worker and runs it in a loop.
     * @param workerPins a set of string that identifies chats and kinds of events this worker has processed. This
     *        allows to route notifications of the same chat to the same worker.
     * @param worker worker callback. accepts notifications. returns false to break.
     */
    template <typename WorkerCallback>
        requires requires(WorkerCallback&& workerCallback, Notification h) {
            { workerCallback(std::move(h)) } -> aui::same_as<AFuture<bool>>;
        }
    AFuture<> run(ASet<AString>& workerPins, WorkerCallback worker) {
        auto workerRegistrationToken = mWorkers.insert(mWorkers.end(), Worker { workerPins });
        AUI_DEFER { mWorkers.erase(workerRegistrationToken); };
        while (true) {
            auto handle = nextNotification(workerPins);
            if (!handle) {
                co_await workerRegistrationToken->wakeUp;
                workerRegistrationToken->wakeUp =  AFuture<>();   // reset
                continue;
            }
            handle->notification.message += "\nCurrent time: {} UTC"_format(std::chrono::system_clock::now());
            handle->onStartedProcessing.supplyValue();
            AUI_DEFER { handle->onProcessed.supplyValue(); };
            if (!co_await worker(std::move(handle->notification))) {
                break;
            }
        }
    }

    /**
     * @brief Passes an event to the AI to process
     * Think of it as your phone's notifications: you receive a notification, read it and (maybe) react to it.
     */
    const NotificationHandle& passNotificationToAI(Notification notification);

    /**
     * @brief Removes notifications by the given substring.
     * @param substring to search in notification texts. Must be unique enough to avoid false positives.
     * @details
     * Can be used to remove obsolete notifications from the queue.
     */
    void removeNotifications(const AString& substring);

private:
    std::deque<NotificationHandle> mNotifications;
    struct Worker {
        const ASet<AString>& pins;
        AFuture<> wakeUp;
    };
    std::list<Worker> mWorkers;

    /**
     * @brief Finds next notification to process.
     * @param pins Worker's pins.
     * @return
     */
    AOptional<NotificationHandle> nextNotification(ASet<AString>& pins);

};