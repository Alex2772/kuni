#pragma once

#include "IOpenAIChat.h"
#include "AUI/Thread/AFuture.h"
#include <chrono>
#include <span>

namespace llmui {

/**
 * @brief A single transcribed segment from a video, either a visual frame or an audio segment.
 */
struct Frame {
    AString track;                  ///< "video" or "audio"
    std::chrono::milliseconds from; ///< segment start time
    std::chrono::milliseconds to;   ///< segment end time
    AString contents;               ///< LLM description (video) or transcript text (audio)
};

/**
 * @brief Describes a video file and returns a merged, time-ordered list of video and audio frames.
 *
 * Extracts up to 16 frames evenly distributed over the video duration using libavformat/libavcodec,
 * then processes them sequentially with a chain-of-thought approach (each LLM call sees the current
 * frame and the previous frame's description). For non-sticker videos, also transcribes the audio
 * track if capabilityHearing is enabled. The resulting video frames and audio segments are merged
 * in chronological order.
 *
 * @param temporaryContext  Recent chat context for the LLM.
 * @param openAI            OpenAI-compatible chat endpoint.
 * @param pathToVideo       Local filesystem path to the video file.
 * @param isSticker         If true, treats as a sticker (no audio transcription, different prompt).
 */
AFuture<AVector<Frame>> videoFrames(std::span<const IOpenAIChat::Message> temporaryContext,
                                    IOpenAIChat& openAI,
                                    AStringView pathToVideo,
                                    bool isSticker = false);

/**
 * @brief Describes a video file using vision LLM + optional audio transcription.
 *
 * Calls videoFrames() and formats the result as LLM-friendly XML. Result is cached in
 * cache/videos/<filename>.md.
 *
 * @param temporaryContext  Recent chat context for the LLM.
 * @param openAI            OpenAI-compatible chat endpoint.
 * @param pathToVideo       Local filesystem path to the video file.
 * @param xmlTag            XML tag name wrapping the output (e.g. "video", "sticker").
 */
AFuture<AString> video(std::span<const IOpenAIChat::Message> temporaryContext,
                       IOpenAIChat& openAI,
                       AStringView pathToVideo,
                       AStringView xmlTag = "video");

}  // namespace llmui
