//
// Created by alex2772 on 7/2/26.
//

#include "video.h"

#include <optional>

#include "audio.h"
#include "config.h"
#include "prompts.h"
#include "AUI/IO/AFileInputStream.h"
#include "AUI/IO/AFileOutputStream.h"
#include "AUI/Image/jpg/JpgImageLoader.h"
#include "AUI/Util/kAUI.h"


static constexpr auto LOG_TAG = "llmui::video";

#if KUNI_USE_FFMPEG

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}


// RAII wrappers for ffmpeg resources
namespace {

struct FormatContextDeleter {
    void operator()(AVFormatContext* ctx) const {
        if (ctx) avformat_close_input(&ctx);
    }
};
using FormatContextPtr = std::unique_ptr<AVFormatContext, FormatContextDeleter>;

struct CodecContextDeleter {
    void operator()(AVCodecContext* ctx) const {
        if (ctx) avcodec_free_context(&ctx);
    }
};
using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;

struct FrameDeleter {
    void operator()(AVFrame* f) const {
        if (f) av_frame_free(&f);
    }
};
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;

struct PacketDeleter {
    void operator()(AVPacket* p) const {
        if (p) av_packet_free(&p);
    }
};
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;

struct SwsContextDeleter {
    void operator()(SwsContext* ctx) const {
        if (ctx) sws_freeContext(ctx);
    }
};
using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;

struct FrameResources {
    FramePtr rgbaFrame{av_frame_alloc()};
    AByteBuffer rgbaBuffer;
};

// Allocate reusable RGBA frame resources for the given output dimensions.
FrameResources makeFrameResources(int outWidth, int outHeight) {
    FrameResources res;
    int bufSize = av_image_get_buffer_size(AV_PIX_FMT_RGBA, outWidth, outHeight, 1);
    // resize() sets both capacity and size, so AByteBufferView will report correct size
    res.rgbaBuffer.resize(bufSize);
    av_image_fill_arrays(res.rgbaFrame->data, res.rgbaFrame->linesize,
                         reinterpret_cast<uint8_t*>(res.rgbaBuffer.data()),
                         AV_PIX_FMT_RGBA, outWidth, outHeight, 1);
    res.rgbaFrame->width  = outWidth;
    res.rgbaFrame->height = outHeight;
    res.rgbaFrame->format = AV_PIX_FMT_RGBA;
    return res;
}

// Decode a single video frame at the given stream timestamp.
// rgbaFrame/rgbaBuffer must be pre-allocated via makeFrameResources().
// Returns an AImageView into res.rgbaBuffer (valid until next call), or nullopt on failure.
std::optional<AImageView> decodeFrameAt(AVFormatContext* fmtCtx, AVCodecContext* codecCtx, int videoStreamIdx,
                                        int64_t targetPts, SwsContext* swsCtx, FrameResources& res) {
    // Seek to the target position
    av_seek_frame(fmtCtx, videoStreamIdx, targetPts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(codecCtx);

    PacketPtr pkt(av_packet_alloc());
    FramePtr  frame(av_frame_alloc());

    // Seek lands on a keyframe *before* targetPts; keep decoding until we
    // reach a frame whose PTS is >= targetPts so each call returns a distinct frame.
    // If we hit EOF before reaching targetPts, the last decoded frame is used as fallback.
    bool gotFrame = false;
    auto tryDecodeFrame = [&](AVFrame* f) -> bool {
        if (f->pts != AV_NOPTS_VALUE && f->pts < targetPts) {
            // Not at target yet — but save it as a fallback in case we hit EOF
            sws_scale(swsCtx, f->data, f->linesize, 0, codecCtx->height,
                      res.rgbaFrame->data, res.rgbaFrame->linesize);
            av_frame_unref(f);
            return false;  // keep draining
        }
        sws_scale(swsCtx, f->data, f->linesize, 0, codecCtx->height,
                  res.rgbaFrame->data, res.rgbaFrame->linesize);
        av_frame_unref(f);
        return true;
    };

    bool hadAnyFrame = false;
    while (av_read_frame(fmtCtx, pkt.get()) >= 0) {
        AUI_DEFER { av_packet_unref(pkt.get()); };

        if (pkt->stream_index != videoStreamIdx) {
            continue;
        }

        if (avcodec_send_packet(codecCtx, pkt.get()) < 0) {
            continue;
        }

        while (avcodec_receive_frame(codecCtx, frame.get()) >= 0) {
            hadAnyFrame = true;
            if (tryDecodeFrame(frame.get())) {
                gotFrame = true;
                break;
            }
        }
        if (gotFrame) break;
    }

    if (!gotFrame) {
        // Flush any buffered frames from the codec
        avcodec_send_packet(codecCtx, nullptr);
        while (avcodec_receive_frame(codecCtx, frame.get()) >= 0) {
            hadAnyFrame = true;
            tryDecodeFrame(frame.get());  // always scale; last one wins
        }
        // If we decoded at least one frame (even before targetPts), use it as fallback
        gotFrame = hadAnyFrame;
    }

    if (!gotFrame) {
        return std::nullopt;
    }

    return AImageView(res.rgbaBuffer,
                      glm::uvec2{static_cast<unsigned>(res.rgbaFrame->width),
                                 static_cast<unsigned>(res.rgbaFrame->height)},
                      APixelFormat::RGBA_BYTE);
}

}  // anonymous namespace

#endif

// Format milliseconds as "M:SS.mmm" (omitting trailing zeros after decimal point)
static AString formatTimestamp(std::chrono::milliseconds ms) {
    auto totalMs = ms.count();
    auto minutes = totalMs / 60000;
    auto secs    = (totalMs % 60000) / 1000;
    auto millis  = totalMs % 1000;
    if (millis == 0) {
        return "{}:{:02d}"_format(minutes, secs);
    }
    // Trim trailing zeros
    AString fracStr = "{:03d}"_format(millis);
    while (fracStr.endsWith("0")) {
        fracStr = fracStr.substr(0, fracStr.length() - 1);
    }
    return "{}:{:02d}.{}"_format(minutes, secs, fracStr);
}

AFuture<AVector<llmui::Frame>> llmui::videoFrames(std::span<const IOpenAIChat::Message> temporaryContext,
                                                   IOpenAIChat& openAI,
                                                   AStringView pathToVideo,
                                                   bool isSticker) {
#if KUNI_USE_FFMPEG
    try {
        // --- Open the video file ---
        AVFormatContext* rawFmtCtx = nullptr;
        if (avformat_open_input(&rawFmtCtx, pathToVideo.toStdString().c_str(), nullptr, nullptr) < 0) {
            ALogger::warn(LOG_TAG) << "avformat_open_input failed for: " << pathToVideo;
            co_return AVector<Frame>{};
        }
        FormatContextPtr fmtCtx(rawFmtCtx);

        if (avformat_find_stream_info(fmtCtx.get(), nullptr) < 0) {
            co_return AVector<Frame>{};
        }

        // --- Find video stream ---
        int videoStreamIdx = av_find_best_stream(fmtCtx.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (videoStreamIdx < 0) {
            co_return AVector<Frame>{};
        }

        AVStream* videoStream = fmtCtx->streams[videoStreamIdx];
        const AVCodec* codec = avcodec_find_decoder(videoStream->codecpar->codec_id);
        if (!codec) {
            co_return AVector<Frame>{};
        }

        CodecContextPtr codecCtx(avcodec_alloc_context3(codec));
        avcodec_parameters_to_context(codecCtx.get(), videoStream->codecpar);
        if (avcodec_open2(codecCtx.get(), codec, nullptr) < 0) {
            co_return AVector<Frame>{};
        }

        // --- Compute duration ---
        double durationSecs = 0.0;
        if (videoStream->duration != AV_NOPTS_VALUE) {
            durationSecs = videoStream->duration * av_q2d(videoStream->time_base);
        } else if (fmtCtx->duration != AV_NOPTS_VALUE) {
            durationSecs = static_cast<double>(fmtCtx->duration) / AV_TIME_BASE;
        }

        // Sample at most one frame per video_min_step_ms, capped at video_max_frames overall.
        const double durationMs = durationSecs * 1000.0;
        const int frameCount = (durationMs > 0.0)
            ? std::clamp(static_cast<int>(std::ceil(durationMs / static_cast<double>(config().videoMinStepMs))),
                         1, static_cast<int>(config().videoMaxFrames))
            : 1;



        // --- Setup scaler (source pix_fmt → RGBA, scale to 512px) ---
        const int srcW = codecCtx->width;
        const int srcH = codecCtx->height;
        const int outW = 512;
        const int outH = 512;

        SwsContextPtr swsCtx(sws_getContext(
            srcW, srcH, codecCtx->pix_fmt,
            outW, outH, AV_PIX_FMT_RGBA,
            SWS_BILINEAR, nullptr, nullptr, nullptr));

        if (!swsCtx) {
            co_return AVector<Frame>{};
        }

        const AString& framePrompt = "You are video-to-text captioning module. Don't overthink, provide response as fast as possible.";

        // --- Chain-of-thought frame processing ---
        AVector<Frame> videoFramesList;
        AString prevFrameEmbedding;

        // Allocate RGBA frame buffer once and reuse across all frames
        auto frameRes = makeFrameResources(outW, outH);

        for (int i = 0; i < frameCount; ++i) {
            // Compute time for this frame
            double frameSec = 0.0;
            int64_t targetPts = 0;
            if (frameCount > 1 && durationSecs > 0.0) {
                frameSec   = durationSecs * i / (frameCount - 1);
                targetPts  = static_cast<int64_t>(frameSec / av_q2d(videoStream->time_base));
            }

            double nextFrameSec = (frameCount > 1 && durationSecs > 0.0 && i + 1 < frameCount)
                ? durationSecs * (i + 1) / (frameCount - 1)
                : frameSec + 1.0;

            auto frameImage = decodeFrameAt(fmtCtx.get(), codecCtx.get(), videoStreamIdx,
                                            targetPts, swsCtx.get(), frameRes);
            if (!frameImage) {
                ALogger::warn(LOG_TAG) << "Failed to decode frame " << i << " for " << pathToVideo;
                continue;
            }
            JpgImageLoader::save(AFileOutputStream("frame{}.jpg"_format(i)), *frameImage);

            // Build per-frame context
            AString frameContext;

            auto currentEmbedding = IOpenAIChat::embedImage(*frameImage);
            /*if (!prevFrameEmbedding.empty()) {
                frameContext += "<prev_frame>\n";
                frameContext += prevFrameEmbedding;
                frameContext += "\n</prev_frame>\n";
                frameContext += "<current_frame>\n";
                frameContext += currentEmbedding;
                frameContext += "\n</current_frame>\n";
                frameContext += "\nDescribe what changed compared to the previous frame.";
            } else */{
                frameContext += currentEmbedding;
                frameContext += "\n\nDescribe this frame briefly.";
            }
            AUI_DEFER { prevFrameEmbedding = std::move(currentEmbedding); };

            tryAgain:
            auto response = co_await openAI.chat({
                .systemPrompt = framePrompt,
                .config = config().llmImageToText,
            }, { { .role = IOpenAIChat::Message::Role::USER, .content = frameContext } });

            auto currentFrameDescription = std::move(response.choices.at(0).message.content);
            if (currentFrameDescription.trim().empty()) {
                goto tryAgain;
            }

            videoFramesList.push_back(Frame{
                .track    = "video",
                .from     = std::chrono::milliseconds(static_cast<int64_t>(frameSec * 1000)),
                .to       = std::chrono::milliseconds(static_cast<int64_t>(nextFrameSec * 1000)),
                .contents = std::move(currentFrameDescription),
            });
        }

        // --- Audio transcription (non-sticker only) ---
        AVector<Frame> audioFrames;
        if (!isSticker && config().capabilityHearing) {
            int audioStreamIdx = av_find_best_stream(fmtCtx.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
            if (audioStreamIdx >= 0) {
                AVStream* inAudioStream = fmtCtx->streams[audioStreamIdx];
                // Pick a container that natively supports the audio codec to allow direct remux.
                // opus  → webm, aac → mp4, everything else → matroska (accepts any codec).
                const char* outFmtName = "matroska";
                AString transcribeFormat = "ogg";
                switch (inAudioStream->codecpar->codec_id) {
                    case AV_CODEC_ID_OPUS:
                        outFmtName = "webm"; transcribeFormat = "webm"; break;
                    case AV_CODEC_ID_AAC:
                        outFmtName = "mp4";  transcribeFormat = "mp4";  break;
                    case AV_CODEC_ID_MP3:
                        outFmtName = "mp3";  transcribeFormat = "mp3";  break;
                    case AV_CODEC_ID_VORBIS:
                        outFmtName = "ogg";  transcribeFormat = "ogg";  break;
                    default: break;  // matroska / ogg fallback
                }

                // Write remuxed audio directly into an AByteBuffer via a custom AVIOContext.
                // Seekable output is required by mp4/webm muxers (they rewrite headers after seeing all data).
                // We track a write cursor separately; seek just moves it; write does a positional overwrite or append.
                struct AvioMemCtx {
                    AByteBuffer buf;
                    int64_t pos = 0;  // current write cursor

                    static int writePacket(void* opaque, const uint8_t* src, int size) {
                        auto* self = static_cast<AvioMemCtx*>(opaque);
                        size_t end = static_cast<size_t>(self->pos) + static_cast<size_t>(size);
                        if (end > self->buf.capacity()) {
                            self->buf.reserve(end * 2);
                        }
                        std::memcpy(self->buf.data() + self->pos, src, size);
                        if (end > self->buf.size()) {
                            self->buf.setSize(end);
                        }
                        self->pos += size;
                        return size;
                    }

                    static int64_t seek(void* opaque, int64_t offset, int whence) {
                        auto* self = static_cast<AvioMemCtx*>(opaque);
                        if (whence == AVSEEK_SIZE) {
                            return static_cast<int64_t>(self->buf.size());
                        }
                        int64_t newPos;
                        if (whence == SEEK_SET)       newPos = offset;
                        else if (whence == SEEK_CUR)  newPos = self->pos + offset;
                        else /* SEEK_END */           newPos = static_cast<int64_t>(self->buf.size()) + offset;
                        if (newPos < 0) return AVERROR(EINVAL);
                        self->pos = newPos;
                        return self->pos;
                    }
                };
                AvioMemCtx avioMem;
                constexpr int AVIO_BUF_SIZE = 65536;
                uint8_t* avioBuf = static_cast<uint8_t*>(av_malloc(AVIO_BUF_SIZE));
                AVIOContext* avioCtx = avio_alloc_context(avioBuf, AVIO_BUF_SIZE,
                    /*write_flag=*/1, &avioMem,
                    /*read_packet=*/nullptr,
                    AvioMemCtx::writePacket,
                    AvioMemCtx::seek);
                if (!avioCtx) {
                    av_free(avioBuf);
                } else {
                    AUI_DEFER {
                        av_free(avioCtx->buffer);
                        avio_context_free(&avioCtx);
                    };

                    AVFormatContext* outFmtCtx = nullptr;
                    if (avformat_alloc_output_context2(&outFmtCtx, nullptr, outFmtName, nullptr) >= 0) {
                        struct OutFmtCtxDeleter {
                            void operator()(AVFormatContext* c) const {
                                if (c) avformat_free_context(c);
                            }
                        };
                        std::unique_ptr<AVFormatContext, OutFmtCtxDeleter> outCtx(outFmtCtx);

                        // Attach our custom AVIO; mark AVFMT_FLAG_CUSTOM_IO so FFmpeg won't try to open a file.
                        outCtx->pb = avioCtx;
                        outCtx->flags |= AVFMT_FLAG_CUSTOM_IO;

                        AVStream* outStream = avformat_new_stream(outCtx.get(), nullptr);

                        if (outStream && avcodec_parameters_copy(outStream->codecpar, inAudioStream->codecpar) >= 0) {
                            outStream->codecpar->codec_tag = 0;

                            if (avformat_write_header(outCtx.get(), nullptr) >= 0) {
                                av_seek_frame(fmtCtx.get(), audioStreamIdx, 0, AVSEEK_FLAG_BACKWARD);

                                PacketPtr pkt(av_packet_alloc());
                                while (av_read_frame(fmtCtx.get(), pkt.get()) >= 0) {
                                    AUI_DEFER { av_packet_unref(pkt.get()); };
                                    if (pkt->stream_index != audioStreamIdx) continue;

                                    av_packet_rescale_ts(pkt.get(),
                                                         inAudioStream->time_base,
                                                         outStream->time_base);
                                    pkt->stream_index = 0;
                                    av_interleaved_write_frame(outCtx.get(), pkt.get());
                                }
                                av_write_trailer(outCtx.get());

                                try {
                                    auto transcription = co_await openAI.transcribeAudio(avioMem.buf, transcribeFormat);

                                    // Convert each audio segment to a Frame
                                    for (const auto& seg : transcription.segments) {
                                        if (seg.text.trim().empty()) continue;
                                        audioFrames.push_back(Frame{
                                            .track    = "audio",
                                            .from     = std::chrono::milliseconds(static_cast<int64_t>(seg.start * 1000)),
                                            .to       = std::chrono::milliseconds(static_cast<int64_t>(seg.end * 1000)),
                                            .contents = seg.text.trim(),
                                        });
                                    }
                                } catch (const AException& e) {
                                    ALogger::warn(LOG_TAG) << "Audio transcription failed: " << e;
                                }
                            }
                        }
                    }
                }
            }
        }

        // --- Merge video and audio frames in chronological order ---
        AVector<Frame> merged;
        merged.reserve(videoFramesList.size() + audioFrames.size());

        size_t vi = 0, ai = 0;
        while (vi < videoFramesList.size() || ai < audioFrames.size()) {
            bool takeVideo = vi < videoFramesList.size() &&
                (ai >= audioFrames.size() || videoFramesList[vi].from <= audioFrames[ai].from);
            if (takeVideo) {
                merged.push_back(std::move(videoFramesList[vi++]));
            } else {
                merged.push_back(std::move(audioFrames[ai++]));
            }
        }

        co_return merged;

    } catch (const AException& e) {
        ALogger::err(LOG_TAG) << "Can't describe video frames: " << e;
        co_return AVector<Frame>{};
    }
#else
    throw AException("unsupported");
#endif
}

AFuture<AString> llmui::video(std::span<const IOpenAIChat::Message> temporaryContext,
                               IOpenAIChat& openAI,
                               AStringView pathToVideo,
                               AStringView xmlTag) {
    static const APath CACHE_DIR = APath("cache") / "videos";
    AUI_DO_ONCE { CACHE_DIR.makeDirs(); }

    APath cache = CACHE_DIR / "{}.md"_format(APath(pathToVideo).filename());

    try {
#ifndef AUI_TESTS_MODULE
        if (cache.isRegularFileExists()) {
            auto content = AString::fromUtf8(AByteBuffer::fromStream(AFileInputStream(cache)));
            co_return "<{} transcription>\n{}\n</{}>"_format(xmlTag, std::move(content), xmlTag);
        }
#endif

        const bool isSticker = xmlTag.contains("sticker");
        auto frames = co_await llmui::videoFrames(temporaryContext, openAI, pathToVideo, isSticker);

        if (frames.empty()) {
            co_return "<{} description>\nThis media type is not supported\n</{}>"_format(xmlTag, xmlTag);
        }

        // Merge consecutive frames of the same track
        AVector<Frame> mergedFrames;
        mergedFrames.reserve(frames.size());
        for (auto& f : frames) {
            if (!mergedFrames.empty() && mergedFrames.back().track == f.track) {
                mergedFrames.back().to = f.to;
                mergedFrames.back().contents += " ";
                mergedFrames.back().contents += f.contents;
            } else {
                mergedFrames.push_back(std::move(f));
            }
        }

        // Format frames as LLM-friendly XML
        AString result;
        for (const auto& f : mergedFrames) {
            if (!result.empty()) result += "\n";
            result += "<f track=\"{}\" from=\"{}\" to=\"{}\">\n{}\n</f>"_format(
                f.track,
                formatTimestamp(f.from),
                formatTimestamp(f.to),
                f.contents);
        }

        AFileOutputStream(cache) << static_cast<AString&>(result);
        co_return "<{} description>\n{}\n</{}>"_format(xmlTag, std::move(result), xmlTag);

    } catch (const AException& e) {
        ALogger::err(LOG_TAG) << "Can't describe video: " << e;
        co_return "<{} description>\nThis media type is not supported\n</{}>"_format(xmlTag, xmlTag);
    }
}
