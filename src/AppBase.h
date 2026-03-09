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
     * @return Promise satisfied when the notification is processed.
     * @details
     * Think of it as your phone's notifications: you receive a notification, read it and (maybe) react to it.
     */
    const AFuture<>& passNotificationToAI(AString notification, OpenAITools actions = {});

    AFuture<> diaryDumpMessages();

    void actProactively();


    struct DiaryEntry {
        AString id;
        AString text;
    };

    struct DiaryEntryEx {
        AString id;
        struct Metadata {
            float score = 0.f;
            AString lastUsed = "never";
            int usageCount = 0;
            std::valarray<float> embedding;
        } metadata;
        AString freeformBody;

        void incrementUsageCount() {
            metadata.usageCount++;
            metadata.lastUsed = "{}"_format(std::chrono::system_clock::now());
        }
    };

    struct DiaryEntryExAndRelatedness {
        std::list<DiaryEntryEx>::iterator entry;
        aui::float_within_0_1 relatedness;
    };

    [[nodiscard]] const AVector<OpenAIChat::Message>& temporaryContext() const { return mTemporaryContext; }

    AFuture<AVector<DiaryEntryExAndRelatedness>> diaryQuery(const std::valarray<float>& query);

protected:
    AAsyncHolder mAsync;

    aui::float_within_0_1 mRelevanceThreshold = 0.5f;

    /**
     * @brief Returns diary entries that was saved previously by diarySave.
     */
    virtual AVector<DiaryEntry> diaryRead() const {
        return {};
    }

    virtual void diarySave(const DiaryEntry& dairyEntry) {
        ALogger::warn("AppBase") << "diarySave stub (" << dairyEntry.text << ")";
    }

    virtual AFuture<aui::float_within_0_1> diaryEntryIsRelated(const std::valarray<float>& context, DiaryEntryEx& entry);

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
        AFuture<> onProcessed;
    };
    std::queue<Notification> mNotifications;
    AFuture<> mNotificationsSignal;
    _<ATimer> mWakeupTimer;
    // OpenAITools mTools;
    static std::list<DiaryEntryEx> diaryParse(AVector<DiaryEntry> diary);

    void diarySave(const DiaryEntryEx& dairyEntry);

    aui::lazy<std::list<DiaryEntryEx>> mCachedDiary = [this]{ return diaryParse(diaryRead()); };

    AVector<OpenAIChat::Message> mTemporaryContext {};


};

