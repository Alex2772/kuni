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

    AFuture<> diaryDumpMessages();

    void actProactively();

protected:
    AAsyncHolder mAsync;
    virtual AFuture<> telegramPostMessage(int64_t chatId, AString text) {
        ALogger::warn("AppBase") << "telegramPostMessage stub (" << chatId << ", " << text << ")";
        co_return;
    }

    /**
     * @brief Returns diary entries that was saved previously by diarySave.
     */
    virtual AVector<AString> diaryRead() const {
        return {};
    }

    virtual void diarySave(const AString& message) {
        ALogger::warn("AppBase") << "diarySave stub (" << message << ")";
    }

    virtual AFuture<bool> diaryEntryIsRelatedToCurrentContext(const AString& diaryEntry);

    /**
     * @brief Adds always available actions
     */
    virtual void updateTools(OpenAITools& actions) {}

    /**
     * @brief Removes notifications by the given substring.
     * @param substring to search in notification texts. Must be unique enough to avoid false positives.
     * @details
     * Can be used to remove obsolete notifications from AI's queue.
     */
    void removeNotifications(const AString& substring);

private:
    struct Notification {
        AString message;
        OpenAITools actions;
    };
    std::queue<Notification> mNotifications;
    AFuture<> mNotificationsSignal;
    _<ATimer> mWakeupTimer;
    // OpenAITools mTools;
    aui::lazy<AVector<AString>> mCachedDiary = [this]{ return diaryRead(); };

    AVector<OpenAIChat::Message> mTemporaryContext {};


};

