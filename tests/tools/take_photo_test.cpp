// Created by alex2772 on 5/9/26.
//

#include "tools/take_photo.h"
#include "../common.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AEventLoop.h"
#include "util/await_synchronously.h"

#include <gmock/gmock.h>

namespace {
// ---------------------------------------------------------------------------
// Mock IOpenAIChat
// ---------------------------------------------------------------------------
class OpenAIMock : public IOpenAIChat {
public:
    MOCK_METHOD(_<StreamingResponse>, chatStreaming, (Params params, IOpenAIChat::Session messages), (override));
    MOCK_METHOD(AFuture<std::valarray<double>>, embedding, (Params params, AString input), (override));
};

// ---------------------------------------------------------------------------
// Mock IStableDiffusionClient
// ---------------------------------------------------------------------------
class StableDiffusionMock : public IStableDiffusionClient {
public:
    MOCK_METHOD(AFuture<Txt2ImgResponse>, txt2img, (const Txt2ImgRequest& request), (override));
    MOCK_METHOD(AFuture<>, unloadCheckpoint, (), (override));
};
}

// ===========================================================================
// takePhoto – Missing photo_desc throws
// ===========================================================================
TEST(TakePhotoTest, MissingPhotoDescThrows) {
    auto sd = _new<StableDiffusionMock>();
    auto openAI = _new<OpenAIMock>();

    // No calls expected — handler should throw before reaching any API
    EXPECT_CALL(*sd, txt2img(testing::_)).Times(0);
    EXPECT_CALL(*openAI, chatStreaming(testing::_, testing::_)).Times(0);

    auto tool = tools::takePhoto(std::move(sd), std::move(openAI));

    OpenAITools tools{};
    EXPECT_THROW(
        util::await_synchronously(tool.handler({
            .tools = tools,
            .args = AJson::Object{},  // empty args, no "photo_desc"
            .allToolCalls = {},
        })),
        AException
    );
}

// ===========================================================================
// takePhoto – photo_desc is not a string throws
// ===========================================================================
TEST(TakePhotoTest, PhotoDescNotStringThrows) {
    auto sd = _new<StableDiffusionMock>();
    auto openAI = _new<OpenAIMock>();

    EXPECT_CALL(*sd, txt2img(testing::_)).Times(0);
    EXPECT_CALL(*openAI, chatStreaming(testing::_, testing::_)).Times(0);

    auto tool = tools::takePhoto(std::move(sd), std::move(openAI));

    OpenAITools tools{};
    EXPECT_THROW(
        util::await_synchronously(tool.handler({
            .tools = tools,
            .args = AJson::Object{{"photo_desc", 123}},
            .allToolCalls = {},
        })),
        AException
    );
}
