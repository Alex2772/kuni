//
// Created by alex2772 on 5/9/26.
//

#include "llmui/audio.h"
#include "../common.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AEventLoop.h"
#include "util/await_synchronously.h"

#include <gmock/gmock.h>

using namespace testing;

// ===========================================================================
// llmui::voiceMessage – Nonexistent file returns empty string
// ===========================================================================
TEST(LlmuiAudioTest, NonexistentFileReturnsEmpty) {
    // voiceMessage tries to open the file, which will throw.
    // The exception is caught and an empty string is returned.
    auto result = util::await_synchronously(llmui::voiceMessage("/nonexistent/path/to/voice.ogg"));

    EXPECT_TRUE(result.empty());
}
