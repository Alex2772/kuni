#include "ImageGenerator.h"
#include "StableDiffusionClientImpl.h"
#include "OpenAIChatImpl.h"
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
        auto sdClient = _new<StableDiffusionClientImpl>();
        sdClient->endpoint = config().sdEndpoint;
        IOpenAIChat::Params chatParams{
            .config = config().llmImageToText,
        };

        ImageGenerator generator(std::move(sdClient), _new<OpenAIChatImpl>(), std::move(chatParams));

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

    while (!async.empty()) {
        loop.iteration();
    }
}
