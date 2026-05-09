//
// Created by alex2772 on 5/9/26.
//

#include "tools/ask_diary.h"
#include "../common.h"
#include "Diary.h"
#include "IOpenAIChat.h"
#include "OpenAITools.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AEventLoop.h"

#include <gmock/gmock.h>

using namespace testing;

namespace {
// ---------------------------------------------------------------------------
// Mock Diary
// ---------------------------------------------------------------------------
class DiaryMock : public Diary {
public:
    DiaryMock()
        : Diary(Init{
              .diaryDir = "/tmp/kuni_test_diary",
              .openAI = nullptr, // not needed for these tests
          }) {}

    MOCK_METHOD(AFuture<AString>, queryAI, (const AString& query, QueryOpts opts), (override));
};
}

// ===========================================================================
// askDiary – Handler: success case
// ===========================================================================
TEST(AskDiaryTest, HandlerSuccess) {
    DiaryMock diary;
    AVector<IOpenAIChat::Message> temporaryContext;
    auto tool = tools::askDiary(temporaryContext, diary);

    const AString kDiaryResponse = "Found relevant memory about user's music hobby.";

    EXPECT_CALL(diary, queryAI(testing::_, testing::_))
        .WillOnce(Return(AFuture<AString>(kDiaryResponse)));

    OpenAITools tools{};

    auto result = await(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"query", "What songs does Alex write?"}},
        .allToolCalls = {},
    }));

    EXPECT_TRUE(result.contains(kDiaryResponse));
}

// ===========================================================================
// askDiary – Handler: short query returns error message
// ===========================================================================
TEST(AskDiaryTest, HandlerShortQueryError) {
    DiaryMock diary;
    AVector<IOpenAIChat::Message> temporaryContext;
    auto tool = tools::askDiary(temporaryContext, diary);

    // queryAI should NOT be called for short queries
    EXPECT_CALL(diary, queryAI(testing::_, testing::_)).Times(0);

    OpenAITools tools{};

    auto result = await(tool.handler({
        .tools = tools,
        .args = AJson::Object{{"query", "short"}},
        .allToolCalls = {},
    }));

    EXPECT_TRUE(result.contains("too short query")) << "result = " << result;
    EXPECT_TRUE(result.contains("more context")) << "result = " << result;
}
