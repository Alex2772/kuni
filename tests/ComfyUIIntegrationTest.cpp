#include "comfyui.h"
#include "common.h"

#include <gmock/gmock.h>
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AEventLoop.h"
#include "AUI/Json/AJson.h"
#include "AUI/Image/AImage.h"
#include "config.h"
#include "AUI/Image/png/PngImageLoader.h"

// This test requires a running ComfyUI instance with the default "Save Image" workflow's checkpoint
// available (see config().comfyEndpoint, default http://127.0.0.1:8188/). If it's not available, this test
// will fail with a connection error - that's expected in CI/headless environments without ComfyUI installed.
TEST(ComfyUIIntegration, Txt2Img) {
    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    AAsyncHolder async;

    async << []() -> AFuture<> {
        // Minimal "API format" txt2img graph (KSampler + CheckpointLoaderSimple + SaveImage), exported the same
        // way ComfyUI's "Save (API format)" button would produce it.
        AJson workflow = AJson::Object{
            {"3",
             AJson::Object{
                 {"class_type", "KSampler"},
                 {"inputs",
                  AJson::Object{
                      {"seed", 0},
                      {"steps", 4},
                      {"cfg", 2.0},
                      {"sampler_name", "euler"},
                      {"scheduler", "normal"},
                      {"denoise", 1.0},
                      {"model", AJson::Array{"4", 0}},
                      {"positive", AJson::Array{"6", 0}},
                      {"negative", AJson::Array{"7", 0}},
                      {"latent_image", AJson::Array{"5", 0}},
                  }}}},
            {"4",
             AJson::Object{
                 {"class_type", "CheckpointLoaderSimple"},
                 {"inputs", AJson::Object{{"ckpt_name", config().sdCheckpoint}}}}},
            {"5",
             AJson::Object{
                 {"class_type", "EmptyLatentImage"},
                 {"inputs", AJson::Object{{"width", 256}, {"height", 256}, {"batch_size", 1}}}}},
            {"6",
             AJson::Object{
                 {"class_type", "CLIPTextEncode"},
                 {"inputs", AJson::Object{{"text", "anime girl cat ears"}, {"clip", AJson::Array{"4", 1}}}}}},
            {"7",
             AJson::Object{
                 {"class_type", "CLIPTextEncode"},
                 {"inputs", AJson::Object{{"text", "text, watermark"}, {"clip", AJson::Array{"4", 1}}}}}},
            {"8",
             AJson::Object{
                 {"class_type", "VAEDecode"},
                 {"inputs", AJson::Object{{"samples", AJson::Array{"3", 0}}, {"vae", AJson::Array{"4", 2}}}}}},
            {"9",
             AJson::Object{
                 {"class_type", "SaveImage"},
                 {"inputs", AJson::Object{{"filename_prefix", "kuni_test"}, {"images", AJson::Array{"8", 0}}}}}},
        };

        try {
            auto result = co_await comfy::prompt(workflow);
            EXPECT_FALSE(result.images.empty());
            if (!result.images.empty()) {
                EXPECT_GT(result.images.first()->width(), 0);
                EXPECT_GT(result.images.first()->height(), 0);
            }
            PngImageLoader::save(AFileOutputStream("comfyui_tmp.png"), *result.images.first());
        } catch (const AException& e) {
            // If ComfyUI is not running, we expect a connection error - log and mark as non-fatal.
            std::cout << "ComfyUI not running or error: " << e.getMessage() << std::endl;
            GTEST_NONFATAL_FAILURE_("ComfyUI not running or error");
        }
    }();

    while (!async.empty()) {
        loop.iteration();
    }
}

TEST(ComfyUIIntegration, UploadImage) {
    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    AAsyncHolder async;

    async << []() -> AFuture<> {
        try {
            AFormattedImage<APixelFormat::RGBA_BYTE> image({1, 1});
            image.set(glm::ivec2(0, 0), AFormattedColorConverter(AColor{1.f, 1.f, 1.f, 1.f}));
            auto name = co_await comfy::uploadImage(AImage(image), "kuni_test_upload.png");
            EXPECT_FALSE(name.empty());
        } catch (const AException& e) {
            std::cout << "ComfyUI not running or error: " << e.getMessage() << std::endl;
            GTEST_NONFATAL_FAILURE_("ComfyUI not running or error");
        }
    }();

    while (!async.empty()) {
        loop.iteration();
    }
}
