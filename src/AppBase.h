#pragma once
#include "AUI/Common/AObject.h"
#include "AUI/Common/ATimer.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AEventLoop.h"
#include "OpenAITools.h"

class AppBase: public AObject {
public:
    AppBase();



    /**
     * @brief Passes an event to the AI to process
     * @param notification notification text message in natural language (i.e., "you received a message from "...": ...; an
     * @param actions immediate actions (tools) related to the notification (i.e., open related chat)
     * alarm triggerred, etc...)
     * @details
     * Think of it as your phone's notifications: you receive a notification, read it and (maybe) react to it.
     */
    void passNotificationToAI(AString notification, OpenAITools actions = {});

    AFuture<> dairyDumpMessages();

    void actProactively();

protected:
    AAsyncHolder mAsync;
    virtual AFuture<> telegramPostMessage(int64_t chatId, AString text) {
        ALogger::warn("AppBase") << "telegramPostMessage stub (" << chatId << ", " << text << ")";
        co_return;
    }

    /**
     * @brief Returns dairy entries that was saved previously by dairySave.
     */
    virtual AVector<AString> dairyRead() const {
        return {};
    }

    virtual void dairySave(const AString& message) {
        ALogger::warn("AppBase") << "dairySave stub (" << message << ")";
    }

    virtual AFuture<bool> dairyEntryIsRelatedToCurrentContext(const AString& dairyEntry);

private:
    struct Notification {
        AString message;
        OpenAITools actions;
    };
    std::queue<Notification> mNotifications;
    AFuture<> mNotificationsSignal;
    _<ATimer> mWakeupTimer;
    // OpenAITools mTools;
    aui::lazy<AVector<AString>> mCachedDairy = [this]{ return dairyRead(); };

    AVector<OpenAIChat::Message> mTemporaryContext {};


};

