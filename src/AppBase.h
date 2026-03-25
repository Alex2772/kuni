#pragma once
#include "AUI/Common/AObject.h"
#include "AUI/Common/ATimer.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AEventLoop.h"
#include "Diary.h"
#include "OpenAITools.h"

class AppBase: public AObject {
public:
    AppBase(APath workingDir = "test_data");



    /**
     * @brief Passes an event to the AI to process
     * @param notification notification text message in natural language (i.e., "you received a message from "...": ...; an
     * @param actions immediate actions (tools) related to the notification (i.e., open related chat)
     * alarm triggerred, etc...)
     * @return Promise satisfied when the notification is processed.
     * @details
     * Think of it as your phone's notifications: you receive a notification, read it and (maybe) react to it.
     */
    const AFuture<>& passNotificationToAI(AString notification, OpenAITools actions = {});

    AFuture<> diaryDumpMessages();

    void actProactively();

    [[nodiscard]] const AVector<OpenAIChat::Message>& temporaryContext() const { return mTemporaryContext; }

    [[nodiscard]] Diary& diary() { return mDiary; }

protected:
    AAsyncHolder mAsync;

    aui::float_within_0_1 mRelevanceThreshold = 0.5f;

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

    AVector<OpenAIChat::Message> mTemporaryContext {};


    /**
     * @brief Performs needed adjustments to the diary page and removes the page from listing. Formatted contents are
     * returned.
     * @details
     * Adjusts usage count, last used and score fields, according to relatedness.
     *
     * Format's with XML tag with needed attributes.
     *
     * The diary page with new metadata is dropped onto disk and removes from mDiary. This ensures this specific diary
     * page wouldn't be considered and included again until mTemporary context is cleaned via diaryDumpMessages.
     *
     */
    [[nodiscard]]
    AString takeDiaryEntry(const Diary::EntryExAndRelatedness& i);

private:
    struct Notification {
        AString message;
        OpenAITools actions;
        AFuture<> onProcessed;
    };
    std::queue<Notification> mNotifications;
    AFuture<> mNotificationsSignal;
    _<ATimer> mWakeupTimer;
    // OpenAITools mTools;

    Diary mDiary{"diary"};

    static AString getSystemPrompt();

};

