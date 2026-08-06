//
// Created by alex2772 on 7/14/26.
//

#include "NotificationManager.h"

#include "App.h"

#include <range/v3/algorithm/any_of.hpp>
#include <range/v3/algorithm/remove_if.hpp>

static constexpr auto LOG_TAG = "NotificationManager";

const NotificationManager::NotificationHandle&
NotificationManager::passNotificationToAI(Notification notification) {
    ALOG_TRACE(LOG_TAG) << "passNotificationToAI";
    // ordering no longer matters here: effectivePriority() is recomputed dynamically (aging + hot-pin boost) every
    // time nextNotification() looks for work, so a plain insertion at the back is enough - no need to keep the
    // deque sorted by a value that would go stale the moment time passes or a pin cools down.
    const auto& result = mNotifications.emplace_back(NotificationHandle {
        .notification = std::move(notification),
    });

    if (result.notification.pin) {
        // wake up suitable worker based on pin.
        for (const auto& worker : mWorkers) {
            if (worker.pins.contains(*result.notification.pin)) {
                worker.wakeUp.supplyValue();
                return result;
            }
        }
    }

    // wake up first idle worker.
    for (const auto& worker : mWorkers) {
        if (!worker.wakeUp.hasValue()) {
            worker.wakeUp.supplyValue();
            return result;
        }
    }

    return result;

}
bool NotificationManager::contains(const AString& substring) {
    ALOG_TRACE(LOG_TAG) << "removeNotifications: " << substring;
    return ranges::any_of(mNotifications, [&](const NotificationHandle& h) {
        return h.notification.message.contains(substring);
    });
}

void NotificationManager::removeNotifications(const AString& substring) {
    ALOG_TRACE(LOG_TAG) << "removeNotifications: " << substring;
    mNotifications.erase(ranges::remove_if(mNotifications, [&](const NotificationHandle& h) {
        return h.notification.message.contains(substring);
    }), mNotifications.end());
}

int NotificationManager::effectivePriority(const NotificationHandle& handle) const {
    const auto waitSeconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - handle.insertedAt).count();

    // aging: the longer a notification waits, the higher its effective priority climbs - monotonically and
    // deterministically. unlike a random jitter, this guarantees eventual processing (no bad-luck starvation)
    // without needing to touch/resort the queue on every insertion.
    int result = handle.notification.priority + static_cast<int>(waitSeconds * config().priorityAgingPerSecond);

    // hot pin boost: if some worker currently holds this notification's pin, its LLM context/cache for this
    // chat is "warm" right now, so prefer continuing that conversation over unrelated ones - this creates a
    // natural burst of quick back-and-forth replies. Recomputed on every call (not cached at insertion time),
    // so the boost evaporates by itself the moment the worker flushes/loses the pin - no explicit cooldown needed.
    if (handle.notification.pin && ranges::any_of(mWorkers, [&](const Worker& worker) {
            return worker.pins.contains(*handle.notification.pin);
        })) {
        result += config().notificationHotPinBoost;
    }
    return result;
}

AOptional<NotificationManager::NotificationHandle>
NotificationManager::nextNotification(ASet<AString>& pins) {
    auto take = [&](std::deque<NotificationHandle>::const_iterator it) {
        auto notification = std::move(*it);
        const auto eP = effectivePriority(notification);
        mNotifications.erase(it);
        if (notification.notification.pin) {
            pins << *notification.notification.pin;
        }
        ALogger::info(LOG_TAG) << "Handling notification effective_priority=" << eP << " priority=" << notification.notification.priority << " " << notification.notification.message;
        return notification;
    };

    // pick the eligible notification (respecting pin routing, as before) with the highest *dynamic* effective
    // priority - recomputed right now, not the moment it was inserted.
    AOptional<std::deque<NotificationHandle>::const_iterator> best;
    for (auto it = mNotifications.begin(); it != mNotifications.end(); ++it) {
        if (it->notification.pin && !pins.contains(*it->notification.pin) &&
            ranges::any_of(mWorkers, [&](const Worker& worker) { return worker.pins.contains(*it->notification.pin); })) {
            // this notification is pinned to other worker, skip.
            continue;
        }
        if (!best || effectivePriority(*it) > effectivePriority(**best)) {
            best = it;
        }
    }
    if (!best) {
        return std::nullopt;
    }
    return take(*best);
}
