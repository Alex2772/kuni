// Created by alex2772 on 5/9/26.
//

#include "tools/search_photo_in_gallery.h"
#include "../common.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AEventLoop.h"
#include "util/await_synchronously.h"

#include <gmock/gmock.h>

static constexpr auto LOG_TAG = "SearchPhotoInGalleryIntegrationTest";

TEST(SearchPhotoInGalleryIntegrationTest, BasicTest) {
    util::await_synchronously([]() -> AFuture<> {
        auto openAI = _new<OpenAIChatImpl>();
        OpenAITools tools {};
        auto response = co_await tools::searchPhotoInGallery(openAI, {}).handler(OpenAITools::Ctx {
          .tools = tools,
          .args = AJson::Object { { "photo_desc", "Close-up portrait of anime girl with cat ears against starry background." } },
          .temporaryContext = {},
          .allToolCalls = {},
        });
        ALogger::info(LOG_TAG) << "Response: " << response;
        EXPECT_FALSE(response.empty());
        co_return;
    }());
}
