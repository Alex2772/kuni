//
// Created by alex2772 on 5/9/26.
//

#include "llmui/image.h"
#include "IOpenAIChat.h"
#include "../OpenAIMock.h"
#include "OpenAIChatImpl.h"
#include "config.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/IO/AFileOutputStream.h"
#include "AUI/IO/AFileInputStream.h"
#include "AUI/Image/png/PngImageLoader.h"
#include "llmui/video.h"
#include "util/await_synchronously.h"

#include <gmock/gmock.h>
#include <ranges>

using namespace testing;

static const auto TEST_DATA = APath(__FILE__).parent().parent() / "data";

#if KUNI_USE_FFMPEG

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

static constexpr auto LOG_TAG = "LLmuiVideoTest";

TEST(LlmuiVideoTest, Sticker) {
    auto openAI = _new<OpenAIMock>();
    EXPECT_CALL(*openAI, chatStreaming(testing::_, testing::_))
        .Times(AtLeast(3))
        .WillRepeatedly(Return(makeStreamingTextResponse("A test image with a cute cat.")));

    auto videoPath = TEST_DATA / "sticker.webm";
    auto frames = util::await_synchronously(llmui::videoFrames({}, *openAI, videoPath, /*isSticker=*/true));

    EXPECT_FALSE(frames.empty());
    for (const auto& f : frames) {
        EXPECT_EQ(f.track, "video") << "sticker should only have video frames";
        EXPECT_FALSE(f.contents.empty());
        EXPECT_LE(f.from, f.to);
    }
    // verify chronological order
    for (size_t i = 1; i < frames.size(); ++i) {
        EXPECT_LE(frames[i - 1].from, frames[i].from) << "frames must be chronologically ordered";
    }
    ALogger::info(LOG_TAG) << "Sticker frames: " << frames.size();
}

TEST(LlmuiVideoTest, Video) {
    auto openAI = _new<OpenAIMock>();
    EXPECT_CALL(*openAI, chatStreaming(testing::_, testing::_))
        .Times(AtLeast(3))
        .WillRepeatedly(Return(makeStreamingTextResponse("A test image with a cute cat.")));

    // actual audio transcription pulled from OpenAIChatIntegrationTest/AudioTranscription
    static constexpr auto AUDIO_TRANSCRIPTION = R"({"task":"transcribe","language":"ru","language_probability":0.8892,"duration":14.118,"duration_after_vad":9.488,"text":"Самолетик, вперед, телеграм! Да ебаный вред, блядь ты! Заебалый своей телеграмой, блядь!","segments":[{"id":0,"seek":0,"start":0,"end":1.78,"text":"Самолетик, вперед, телеграм!","tokens":[50365,31152,37083,3605,11,32560,2229,11,15619,4953,15837,0,50454],"temperature":0,"avg_logprob":-0.3185,"compression_ratio":1.4766,"no_speech_prob":0},{"id":1,"seek":0,"start":3.22,"end":5.66,"text":"Да ебаный вред, блядь ты!","tokens":[50526,9149,1997,1552,1416,4851,740,10278,11,1268,2873,856,678,5991,0,50648],"temperature":0,"avg_logprob":-0.3185,"compression_ratio":1.4766,"no_speech_prob":0},{"id":2,"seek":0,"start":5.82,"end":9.14,"text":"Заебалый своей телеграмой, блядь!","tokens":[50656,22391,3749,1218,4851,25346,15619,4953,15837,1700,11,1268,2873,856,678,0,50822],"temperature":0,"avg_logprob":-0.3185,"compression_ratio":1.4766,"no_speech_prob":0}]})";

    EXPECT_CALL(*openAI, transcribeAudio(testing::_, testing::_))
        .Times(1)
        .WillOnce(Return(AFuture(aui::from_json<IOpenAIChat::AudioTranscription>(AJson::fromString(AUDIO_TRANSCRIPTION)))));

    auto videoPath = TEST_DATA / "video.mp4";
    auto frames = util::await_synchronously(llmui::videoFrames({}, *openAI, videoPath));

    EXPECT_FALSE(frames.empty());

    // Must contain both video and audio tracks
    bool hasVideo = false, hasAudio = false;
    for (const auto& f : frames) {
        if (f.track == "video") hasVideo = true;
        if (f.track == "audio") hasAudio = true;
        EXPECT_FALSE(f.contents.empty());
        EXPECT_LE(f.from, f.to);
    }
    EXPECT_TRUE(hasVideo);
    EXPECT_TRUE(hasAudio);

    // Verify chronological ordering
    for (size_t i = 1; i < frames.size(); ++i) {
        EXPECT_LE(frames[i - 1].from, frames[i].from) << "frames must be chronologically ordered";
    }

    // Verify the 3 audio segments from the transcription are present
    auto audioFrames = frames | std::views::filter([](const llmui::Frame& f) { return f.track == "audio"; });
    size_t audioCount = std::ranges::distance(audioFrames);
    EXPECT_EQ(audioCount, 3u) << "expected 3 audio segments from the transcription";

    // Check known segment timestamps (from=0ms, to=1780ms for first segment)
    auto it = std::ranges::find_if(frames, [](const llmui::Frame& f) {
        return f.track == "audio" && f.from == std::chrono::milliseconds(0);
    });
    ASSERT_NE(it, frames.end()) << "first audio segment starting at 0ms not found";
    EXPECT_EQ(it->to, std::chrono::milliseconds(1780));
    EXPECT_FALSE(it->contents.empty());

    ALogger::info(LOG_TAG) << "Video frames total: " << frames.size()
                           << " (video=" << (frames.size() - audioCount) << ", audio=" << audioCount << ")";
}

TEST(LlmuiVideoIntegrationTest, Sticker) {
    auto openAI = _new<OpenAIChatImpl>();
    auto videoPath = TEST_DATA / "sticker.webm";
    auto frames = util::await_synchronously(llmui::video({}, *openAI, videoPath));
    ALogger::info(LOG_TAG) << "Sticker frames\n:" << frames;

    EXPECT_FALSE(frames.empty());

    EXPECT_TRUE(frames.contains("track=\"video\""));
    EXPECT_FALSE(frames.contains("track=\"audio\""));
}

TEST(LlmuiVideoIntegrationTest, Video) {
    auto openAI = _new<OpenAIChatImpl>();
    auto videoPath = TEST_DATA / "video.mp4";
    auto frames = util::await_synchronously(llmui::video({}, *openAI, videoPath));
    ALogger::info(LOG_TAG) << "Video frames\n:" << frames;

    EXPECT_FALSE(frames.empty());

    EXPECT_TRUE(frames.contains("track=\"video\""));
    EXPECT_TRUE(frames.contains("track=\"audio\""));
}

#endif
