#include "StableDiffusionClient.h"
#include "common.h"

#include <gmock/gmock.h>
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AEventLoop.h"
#include "config.h"
#include "AUI/Image/png/PngImageLoader.h"

TEST(StableDiffusionIntegrationClient, Txt2Img)
{
    // This test requires a running Stable Diffusion WebUI with API enabled.
    // If it's not available, this test will fail or timeout.
    // We can't really run it in a headless environment without SD, 
    // but we can at least check if it compiles and the JSON is formed correctly (via logs).

    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    AAsyncHolder async;

    async << []() -> AFuture<> {
        StableDiffusionClient client{ .endpoint = config::ENDPOINT_SD };

        try {
            auto response = co_await client.txt2img({
                .prompt = "Anime girl cat ears shoulder-length dark_blue hair messy strands blue eyes small nose cute fangs. Shoulders and chest are bare. Floating particles in the air.",
                .steps = 5, // small number for test
                .width = 512,
                .height = 512,
            });
            EXPECT_EQ(response.images.size(), 1);
            const auto& image = response.images[0];
            PngImageLoader::save(AFileOutputStream{ "out.png" }, *image);
            EXPECT_EQ(image->width(), 512);
            EXPECT_EQ(image->height(), 512);
        } catch (const AException& e) {
            // If SD is not running, we expect a connection error.
            // We just log it for now.
            std::cout << "SD not running or error: " << e.getMessage() << std::endl;
            GTEST_NONFATAL_FAILURE_("SD not running or error");
        }
    }();

    while (async.size() > 0) {
        loop.iteration();
    }
}
