#include "VoiceGenerator.h"
#include "config.h"
#include "ElevenLabsClient.h"
#include "OpenAISpeechClient.h"
#include "AUI/Logging/ALogger.h"
#include "AUI/IO/AFileOutputStream.h"
#include <algorithm>
#include <chrono>
#include <cstdint>

static constexpr auto LOG_TAG = "VoiceGenerator";

#if KUNI_USE_FFMPEG

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

// RAII wrappers for ffmpeg resources (mirrors the ones in llmui/video.cpp).
namespace {

struct OutputContextDeleter {
    void operator()(AVFormatContext* ctx) const {
        if (!ctx)
            return;
        if (ctx->pb)
            avio_closep(&ctx->pb);
        avformat_free_context(ctx);
    }
};
using OutputContextPtr = std::unique_ptr<AVFormatContext, OutputContextDeleter>;

struct CodecContextDeleter {
    void operator()(AVCodecContext* ctx) const {
        if (ctx)
            avcodec_free_context(&ctx);
    }
};
using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;

struct SwrContextDeleter {
    void operator()(SwrContext* ctx) const {
        if (ctx)
            swr_free(&ctx);
    }
};
using SwrContextPtr = std::unique_ptr<SwrContext, SwrContextDeleter>;

struct AudioFifoDeleter {
    void operator()(AVAudioFifo* f) const {
        if (f)
            av_audio_fifo_free(f);
    }
};
using AudioFifoPtr = std::unique_ptr<AVAudioFifo, AudioFifoDeleter>;

struct FrameDeleter {
    void operator()(AVFrame* f) const {
        if (f)
            av_frame_free(&f);
    }
};
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;

struct PacketDeleter {
    void operator()(AVPacket* p) const {
        if (p)
            av_packet_free(&p);
    }
};
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;

constexpr int OPUS_SAMPLE_RATE = 48000;    // Opus always operates at 48 kHz internally
constexpr int64_t OPUS_BIT_RATE = 32000;   // matches the previous "-b:a 32k"

// The encoder's preferred input sample format (libopus: S16; swresample converts the PCM to it).
AVSampleFormat preferredSampleFormat(const AVCodec* codec) {
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    const enum AVSampleFormat* fmts = nullptr;
    if (avcodec_get_supported_config(
            nullptr, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, reinterpret_cast<const void**>(&fmts), nullptr) >= 0 &&
        fmts && fmts[0] != AV_SAMPLE_FMT_NONE) {
        return fmts[0];
    }
#else
    if (codec->sample_fmts) {
        return codec->sample_fmts[0];
    }
#endif
    return AV_SAMPLE_FMT_S16;
}

// Send one frame (or nullptr to flush) to the encoder and mux the resulting packets into oc.
bool encodeAndMux(AVCodecContext* codecCtx, AVFormatContext* oc, AVStream* stream, AVFrame* frame, AVPacket* pkt) {
    if (avcodec_send_frame(codecCtx, frame) < 0) {
        return false;
    }
    for (;;) {
        int ret = avcodec_receive_packet(codecCtx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return true;
        }
        if (ret < 0) {
            return false;
        }
        av_packet_rescale_ts(pkt, codecCtx->time_base, stream->time_base);
        pkt->stream_index = stream->index;
        ret = av_interleaved_write_frame(oc, pkt);
        av_packet_unref(pkt);
        if (ret < 0) {
            return false;
        }
    }
}

}   // namespace

/**
 * @brief Transcodes raw signed-16-bit-LE mono PCM into an OGG/Opus voice note using libav (libopus).
 * @details Some OpenAI-compatible TTS providers (notably Gemini via RouterAI) only return raw PCM, while
 *          Telegram voice notes (inputMessageVoiceNote) require OGG/Opus. The PCM is resampled to Opus'
 *          native 48 kHz and encoded in-process — no external ffmpeg binary is spawned.
 * @return true on success (out written), false otherwise.
 */
static bool transcodePcmToOpus(const AByteBuffer& pcm, const APath& out, int sampleRate) {
    const int inSamples = static_cast<int>(pcm.getSize() / sizeof(int16_t));
    if (inSamples == 0) {
        ALogger::err(LOG_TAG) << "PCM->Opus: empty input";
        return false;
    }

    const AVCodec* codec = avcodec_find_encoder_by_name("libopus");
    if (!codec) {
        codec = avcodec_find_encoder(AV_CODEC_ID_OPUS);
    }
    if (!codec) {
        ALogger::err(LOG_TAG) << "PCM->Opus: no Opus encoder available in this libav build";
        return false;
    }

    // --- Output OGG container ---
    AVFormatContext* rawOc = nullptr;
    if (avformat_alloc_output_context2(&rawOc, nullptr, "ogg", nullptr) < 0 || !rawOc) {
        ALogger::err(LOG_TAG) << "PCM->Opus: can't allocate output context";
        return false;
    }
    OutputContextPtr oc(rawOc);

    AVStream* stream = avformat_new_stream(oc.get(), nullptr);
    if (!stream) {
        return false;
    }

    CodecContextPtr codecCtx(avcodec_alloc_context3(codec));
    if (!codecCtx) {
        return false;
    }
    codecCtx->sample_rate = OPUS_SAMPLE_RATE;
    codecCtx->bit_rate = OPUS_BIT_RATE;
    codecCtx->sample_fmt = preferredSampleFormat(codec);
    av_channel_layout_default(&codecCtx->ch_layout, 1);   // mono
    if (oc->oformat->flags & AVFMT_GLOBALHEADER) {
        codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (avcodec_open2(codecCtx.get(), codec, nullptr) < 0) {
        ALogger::err(LOG_TAG) << "PCM->Opus: avcodec_open2 failed";
        return false;
    }
    if (avcodec_parameters_from_context(stream->codecpar, codecCtx.get()) < 0) {
        return false;
    }
    stream->time_base = AVRational { 1, OPUS_SAMPLE_RATE };

    if (avio_open(&oc->pb, out.toStdString().c_str(), AVIO_FLAG_WRITE) < 0) {
        ALogger::err(LOG_TAG) << "PCM->Opus: can't open output file " << out;
        return false;
    }
    if (avformat_write_header(oc.get(), nullptr) < 0) {
        ALogger::err(LOG_TAG) << "PCM->Opus: write_header failed";
        return false;
    }

    // --- Resampler: mono s16 @ sampleRate -> encoder format @ 48 kHz ---
    AVChannelLayout monoLayout;
    av_channel_layout_default(&monoLayout, 1);
    SwrContext* rawSwr = nullptr;
    if (swr_alloc_set_opts2(
            &rawSwr, &codecCtx->ch_layout, codecCtx->sample_fmt, OPUS_SAMPLE_RATE, &monoLayout, AV_SAMPLE_FMT_S16, sampleRate, 0, nullptr) < 0 ||
        !rawSwr) {
        ALogger::err(LOG_TAG) << "PCM->Opus: swr_alloc_set_opts2 failed";
        return false;
    }
    SwrContextPtr swr(rawSwr);
    if (swr_init(swr.get()) < 0) {
        ALogger::err(LOG_TAG) << "PCM->Opus: swr_init failed";
        return false;
    }

    const int frameSize = codecCtx->frame_size > 0 ? codecCtx->frame_size : 960;   // 20 ms @ 48 kHz
    AudioFifoPtr fifo(av_audio_fifo_alloc(codecCtx->sample_fmt, 1, frameSize));
    PacketPtr pkt(av_packet_alloc());
    if (!fifo || !pkt) {
        return false;
    }

    // Resample a chunk of input (or a flush when inData == nullptr) into the FIFO.
    auto pushResampled = [&](const uint8_t* inData, int nbIn) -> bool {
        int outCount = static_cast<int>(
            av_rescale_rnd(swr_get_delay(swr.get(), sampleRate) + nbIn, OPUS_SAMPLE_RATE, sampleRate, AV_ROUND_UP));
        if (outCount <= 0) {
            return true;
        }
        uint8_t* outData = nullptr;
        if (av_samples_alloc(&outData, nullptr, 1, outCount, codecCtx->sample_fmt, 0) < 0) {
            return false;
        }
        int got = swr_convert(swr.get(), &outData, outCount, inData ? &inData : nullptr, nbIn);
        bool ok = got >= 0 && (got == 0 || av_audio_fifo_write(fifo.get(), reinterpret_cast<void**>(&outData), got) == got);
        av_freep(&outData);
        return ok;
    };

    const auto* pcmData = reinterpret_cast<const uint8_t*>(pcm.data());
    if (!pushResampled(pcmData, inSamples) || !pushResampled(nullptr, 0)) {
        ALogger::err(LOG_TAG) << "PCM->Opus: resampling failed";
        return false;
    }

    // Drain the FIFO into fixed-size Opus frames; the last (short) frame is padded with silence.
    int64_t pts = 0;
    while (av_audio_fifo_size(fifo.get()) > 0) {
        int have = std::min(frameSize, av_audio_fifo_size(fifo.get()));
        FramePtr frame(av_frame_alloc());
        if (!frame) {
            return false;
        }
        frame->nb_samples = frameSize;
        frame->format = codecCtx->sample_fmt;
        frame->sample_rate = OPUS_SAMPLE_RATE;
        av_channel_layout_copy(&frame->ch_layout, &codecCtx->ch_layout);
        if (av_frame_get_buffer(frame.get(), 0) < 0) {
            return false;
        }
        av_samples_set_silence(frame->data, 0, frameSize, 1, codecCtx->sample_fmt);
        if (av_audio_fifo_read(fifo.get(), reinterpret_cast<void**>(frame->data), have) < have) {
            return false;
        }
        frame->pts = pts;
        pts += frameSize;
        if (!encodeAndMux(codecCtx.get(), oc.get(), stream, frame.get(), pkt.get())) {
            ALogger::err(LOG_TAG) << "PCM->Opus: encoding failed";
            return false;
        }
    }

    // Flush the encoder and finalize the container.
    if (!encodeAndMux(codecCtx.get(), oc.get(), stream, nullptr, pkt.get())) {
        return false;
    }
    if (av_write_trailer(oc.get()) < 0) {
        return false;
    }
    return true;
}

#else   // !KUNI_USE_FFMPEG

static bool transcodePcmToOpus(const AByteBuffer&, const APath&, int) {
    ALogger::err(LOG_TAG) << "PCM voice transcoding requires building with -DKUNI_USE_FFMPEG=ON (libav is not compiled "
                             "in)";
    return false;
}

#endif   // KUNI_USE_FFMPEG

AFuture<AByteBuffer> VoiceGenerator::generate(AString text, GenerateOpts opts) {
    switch (config().recordVoiceBackend) {
        case Config::TTSBackend::ELEVENLABS: {
            ElevenLabsClient ttsClient {
                .baseUrl = "https://api.elevenlabs.io/",
                .apiKey = config().recordVoiceElevenLabsKey,
                .voiceId = config().recordVoiceElevenLabsVoice,
            };
            ElevenLabsClient::TextToSpeechRequest request {
                .text = text,
                .model_id = "eleven_v3",
                .language_code = std::move(opts.languageCode),
                .voice_settings = { .speed = opts.speed },
            };
            auto ttsResponse = co_await ttsClient.textToSpeech(request);
            if (ttsResponse.audioData.empty()) {
                throw AException("ElevenLabs returned empty audio data");
            }
            co_return ttsResponse.audioData;
        }
        case Config::TTSBackend::OPENAI: {
            OpenAISpeechClient ttsClient {
                .baseUrl = config().recordVoiceOpenAIUrl,
                .apiKey = config().recordVoiceOpenAIKey,
                .model = config().recordVoiceOpenAIModel,
                .voice = config().recordVoiceOpenAIVoice,
            };
            OpenAISpeechClient::TextToSpeechRequest request {
                .input = text,
                .model = config().recordVoiceOpenAIModel,
                .voice = config().recordVoiceOpenAIVoice,
                .response_format = std::move(opts.responseFormat),
                .num_step = 63,
                .speed = opts.speed,
            };
            auto ttsResponse = co_await ttsClient.textToSpeech(request);
            if (ttsResponse.audioData.empty()) {
                throw AException("OpenAI Speech returned empty audio data");
            }
            co_return ttsResponse.audioData;
        }
    }
    throw AException("unsupported");
}

AFuture<VoiceGenerator::VoiceMessage> VoiceGenerator::generate(AString text, AString languageCode, double speed) {
    ALogger::info(LOG_TAG) << "Generating voice message for text: " << text;

    try {
        APath voiceDir("data/voice_messages");
        voiceDir.makeDirs();

        AByteBuffer audioData = co_await generate(std::move(text), GenerateOpts{
            .languageCode = std::move(languageCode),
            .speed = speed,
        });

        auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();

        // Telegram voice notes require OGG/Opus. Providers that return raw PCM (e.g. Gemini via RouterAI)
        // are transcoded here; anything else is saved as-is.
        const bool needsPcmTranscode =
            config().recordVoiceBackend == Config::TTSBackend::OPENAI && config().recordVoiceOpenAIFormat == "pcm";

        APath outputPath;
        if (needsPcmTranscode) {
            outputPath = voiceDir / "{}.ogg"_format(timestamp);
            if (!transcodePcmToOpus(audioData, outputPath, config().recordVoiceOpenAIPcmSampleRate)) {
                throw AException(
                    "Failed to transcode PCM voice message to OGG/Opus "
                    "(build with -DKUNI_USE_FFMPEG=ON)");
            }
        } else {
            outputPath = voiceDir / "{}.mp3"_format(timestamp);
            AFileOutputStream stream(outputPath);
            stream.write(reinterpret_cast<const char*>(audioData.data()), audioData.getSize());
            stream.close();
        }

        ALogger::info(LOG_TAG) << "Voice message saved to: " << outputPath.absolute();

        co_return VoiceMessage { .path = outputPath.absolute() };
    } catch (const AException& e) {
        ALogger::err(LOG_TAG) << "Failed to generate voice message: " << e;
        throw;
    }
}
