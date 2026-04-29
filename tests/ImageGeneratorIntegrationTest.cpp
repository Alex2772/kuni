#include "ImageGenerator.h"
#include "common.h"

#include <gmock/gmock.h>
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AEventLoop.h"
#include "config.h"
#include "AUI/Image/png/PngImageLoader.h"

TEST(ImageGeneratorIntegration, Generate)
{
    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    AAsyncHolder async;

    async << []() -> AFuture<> {
        StableDiffusionClient sdClient{ .endpoint = config::ENDPOINT_SD };
        OpenAIChat chatClient{ .config = config::ENDPOINT_PHOTO_TO_TEXT };

        ImageGenerator generator(sdClient, chatClient);

        try {
            auto image = (co_await generator.generate("Kuni makes a selfie")).image;
            EXPECT_NE(image, nullptr);
            PngImageLoader::save(AFileOutputStream{ "out_generator.png" }, *image);
            EXPECT_GT(image->width(), 0);
            EXPECT_GT(image->height(), 0);
        } catch (const AException& e) {
            std::cout << "Error: " << e.getMessage() << std::endl;
            GTEST_NONFATAL_FAILURE_("ImageGenerator failed");
        }
    }();

    while (async.size() > 0) {
        loop.iteration();
    }
}
