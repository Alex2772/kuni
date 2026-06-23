//
// Created by alex2772 on 5/9/26.
//

#include "llmui/image.h"
#include "../common.h"
#include "IOpenAIChat.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AEventLoop.h"
#include "AUI/IO/AFileOutputStream.h"
#include "AUI/IO/AFileInputStream.h"
#include "AUI/Image/png/PngImageLoader.h"
#include "util/await_synchronously.h"

#include <gmock/gmock.h>

using namespace testing;

// ---------------------------------------------------------------------------
// Helper: streaming response with plain text content
// ---------------------------------------------------------------------------
static AArc<IOpenAIChat::StreamingResponse> makeStreamingTextResponse(AString content) {
    IOpenAIChat::Message msg;
    msg.role = IOpenAIChat::Message::Role::ASSISTANT;
    msg.content = std::move(content);

    auto result = _new<IOpenAIChat::StreamingResponse>();
    result->response.raw = {
        .choices = {
            IOpenAIChat::Response::Choice{
                .index = 0,
                .message = std::move(msg),
                .finish_reason = "stop",
            },
        },
        .usage = { .prompt_tokens = 10, .completion_tokens = 5, .total_tokens = 15 },
    };
    result->completed.supplyValue();
    return result;
}

// ---------------------------------------------------------------------------
// Mock IOpenAIChat
// ---------------------------------------------------------------------------
class OpenAIMock : public IOpenAIChat {
public:
    MOCK_METHOD((AArc<StreamingResponse>), chatStreaming, (Params params, IOpenAIChat::Session messages), (override));
    MOCK_METHOD(AFuture<std::valarray<double>>, embedding, (Params params, AString input), (override));
};


// ===========================================================================
// llmui::image – Unsupported media type (null image)
// ===========================================================================
TEST(LlmuiImageTest, UnsupportedMediaType) {
    auto openAI = _new<OpenAIMock>();
    // No chat calls expected — image loading fails first
    EXPECT_CALL(*openAI, chatStreaming(testing::_, testing::_)).Times(0);

    IOpenAIChat::Session ctx;
    auto result = util::await_synchronously(llmui::image(ctx, *openAI, "/nonexistent/path/to/image.jpg", "photo"));

    EXPECT_EQ(result, "<photo description>\nThis media type is not supported\n</photo>");
}

// ===========================================================================
// llmui::image – Cache hit returns cached content
// ===========================================================================
TEST(LlmuiImageTest, CacheHit) {
    auto openAI = _new<OpenAIMock>();
    EXPECT_CALL(*openAI, chatStreaming(testing::_, testing::_)).Times(0);

    // Create a dummy image file so image loading succeeds
    auto imagePath = TEST_DATA / "llmui_image_test_dummy.png";
    {
        AImage img({16, 16}, APixelFormat::RGB);
        PngImageLoader::save(AFileOutputStream{imagePath}, img);
    }

    // Create a cache entry
    static constexpr auto CACHED_CONTENT = "Cached response";
    APath cacheDir = APath("cache") / "images";
    cacheDir.makeDirs();
    APath cacheFile = cacheDir / "llmui_image_test_dummy.png.md";
    AFileOutputStream(cacheFile) << CACHED_CONTENT;

    IOpenAIChat::Session ctx;
    auto result = util::await_synchronously(llmui::image(ctx, *openAI, imagePath, "photo"));

    EXPECT_EQ(result, "<photo description>\n{}\n</photo>"_format(CACHED_CONTENT));

    // Cleanup
    imagePath.removeFile();
    cacheFile.removeFile();
}

// ===========================================================================
// llmui::image – Successful description via OpenAI
// ===========================================================================
TEST(LlmuiImageTest, SuccessWithContext) {
    auto openAI = _new<OpenAIMock>();

    // Prepare a dummy image
    auto imagePath = TEST_DATA / "llmui_image_test_dummy2.png";
    {
        AImage img({32, 32}, APixelFormat::RGB);
        PngImageLoader::save(AFileOutputStream{imagePath}, img);
    }

    EXPECT_CALL(*openAI, chatStreaming(testing::_, testing::_))
        .Times(1)
        .WillOnce(Return(makeStreamingTextResponse("A test image with a cute cat.")));

    IOpenAIChat::Session ctx = {
        { .role = IOpenAIChat::Message::Role::USER, .content = "This is context." },
    };
    auto result = util::await_synchronously(llmui::image(ctx, *openAI, imagePath, "photo"));

    EXPECT_EQ(result, "<photo description>\nA test image with a cute cat.\n</photo>");

    // Cleanup cache file created by the function
    APath cacheFile = APath("cache") / "images" / "llmui_image_test_dummy2.png.md";
    if (cacheFile.isRegularFileExists()) {
        cacheFile.removeFile();
    }
    imagePath.removeFile();
}

// ===========================================================================
// llmui::image – Custom XML tag
// ===========================================================================
TEST(LlmuiImageTest, CustomXmlTag) {
    auto openAI = _new<OpenAIMock>();

    auto imagePath = TEST_DATA / "llmui_image_test_dummy3.png";
    {
        AImage img({16, 16}, APixelFormat::RGB);
        PngImageLoader::save(AFileOutputStream{imagePath}, img);
    }

    EXPECT_CALL(*openAI, chatStreaming(testing::_, testing::_))
        .Times(1)
        .WillOnce(Return(makeStreamingTextResponse("A scenic view.")));

    IOpenAIChat::Session ctx;
    auto result = util::await_synchronously(llmui::image(ctx, *openAI, imagePath, "screenshot"));

    EXPECT_EQ(result, "<screenshot description>\nA scenic view.\n</screenshot>");

    APath cacheFile = APath("cache") / "images" / "llmui_image_test_dummy3.png.md";
    if (cacheFile.isRegularFileExists()) {
        cacheFile.removeFile();
    }
    imagePath.removeFile();
}
