//
// Created by alex2772 on 7/14/26.
//

#include "NotificationManager.h"

#include <range/v3/algorithm/any_of.hpp>
#include <range/v3/algorithm/find_if.hpp>
#include <range/v3/algorithm/remove_if.hpp>

static constexpr auto LOG_TAG = "NotificationManager";

const NotificationManager::NotificationHandle&
NotificationManager::passNotificationToAI(Notification notification) {
    ALOG_TRACE(LOG_TAG) << "passNotificationToAI";
    const auto at = ranges::find_if(mNotifications, [&](const NotificationHandle& h) {
        return notification.priority > h.notification.priority;
    });
    const auto& result = *mNotifications.emplace(at, NotificationHandle { .notification = std::move(notification) });

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

void NotificationManager::removeNotifications(const AString& substring) {
    ALOG_TRACE(LOG_TAG) << "removeNotifications: " << substring;
    mNotifications.erase(ranges::remove_if(mNotifications, [&](const NotificationHandle& h) {
        return h.notification.message.contains(substring);
    }), mNotifications.end());
}

AOptional<NotificationManager::NotificationHandle>
NotificationManager::nextNotification(ASet<AString>& pins) {
    auto take = [&](std::deque<NotificationHandle>::const_iterator it) {
        auto notification = std::move(*it);
        mNotifications.erase(it);
        if (notification.notification.pin) {
            pins << *notification.notification.pin;
        }
        return notification;
    };
    for (auto it = mNotifications.begin(); it != mNotifications.end(); ++it) {
        if (!it->notification.pin) {
            return take(it);
        }
        if (pins.contains(*it->notification.pin)) {
            return take(it);
        }
        if (ranges::any_of(mWorkers, [&](const Worker& worker) {
            return worker.pins.contains(*it->notification.pin);
        })) {
            // this notification is pinned to other worker, skip.
            continue;
        }
        return take(it);
    }
    return std::nullopt;
}
