#pragma once
#include "AUI/Common/AObject.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "IOpenAIChat.h"
#include "NotificationManager.h"
#include "Diary.h"

#include <memory>

class AppBase;

class Worker: public AObject {
public:
    Worker(size_t name, AppBase& app);
    ~Worker();

    /**
     * @brief If Kuni is sleeping, this function wake ups her.
     */
    void wakeUpIfSleeping() {
        mWakeUp = true;
    }

    AFuture<> diaryDumpMessages();

private:
    size_t mName;
    ALogger mLogger{"kuni_worker{}.log"_format(mName)};
    AppBase& mApp;
    ASet<AString> mWorkerPins;
    AFuture<> mCoroutine;
    std::shared_ptr<bool> mAliveToken = std::make_shared<bool>(true);
    bool mWakeUp = false;
    bool mAskCalledThisTurn = false;
    aui::float_within_0_1 mRelevanceThreshold = 0.5f;

    IOpenAIChat::Session mTemporaryContext = [this] {
        IOpenAIChat::Session s;
        s.sessionId = "kuni_main_coro({})"_format(mName);
        return s;
    }();

    AFuture<> handleNotification(std::shared_ptr<bool> alive, NotificationManager::Notification notification);

    AString takeDiaryEntry(const Diary::EntryExAndRelatedness& i);

    void updateTools(OpenAITools& tools);
};