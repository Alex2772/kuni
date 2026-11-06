//
// Created by alex2772 on 7/20/26.
//

#include "WhisperLive.h"

#include "AUI/Curl/ACurlMulti.h"
#include "AUI/Json/AJson.h"

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/transform.hpp>

static constexpr auto LOG_TAG = "WhisperLive";

namespace {
struct Packet {
    struct Segment {
        AString start{};
        AString end{};
        AString text;
        bool completed{};
    };

    AString uid;
    AVector<Segment> segments;
    AString message;
};
}

AJSON_FIELDS(Packet,
(uid, "uid", AJsonFieldFlags::OPTIONAL)
(segments, "segments", AJsonFieldFlags::OPTIONAL)
(message, "message", AJsonFieldFlags::OPTIONAL)
);

AJSON_FIELDS(Packet::Segment,
AJSON_FIELDS_ENTRY(start)
AJSON_FIELDS_ENTRY(end)
AJSON_FIELDS_ENTRY(text)
AJSON_FIELDS_ENTRY(completed)
);

WhisperLive::WhisperLive(Config config): mWebsocket(_new<AWebsocket>(std::move(config.endpoint))) {
    connect(mWebsocket->connected, [this, config = std::move(config)] {
        static size_t id = 0;
        AJson json{
            {"uid", "{}"_format(id++)},
            {"language", config.language},
            {"model", config.model},
            {"use_vad", true},
            {"task", "transcribe"},
        };
        mWebsocket->writeText(AJson::toString(json));
        mConnected = true;
    });
    connect(mWebsocket->received, [this](AByteBuffer buffer) {
        try {
            ALOG_TRACE(LOG_TAG) << AString::fromUtf8(buffer);
            auto packet = aui::from_json<Packet>(AJson::fromBuffer(buffer));
            if (!packet.message.empty()) {
                ALogger::info(LOG_TAG) << "Message: " << packet.message;
            }
            emit update(packet.segments | ranges::view::transform([](Packet::Segment& segment) {
                return WhisperLive::Segment {
                    .start = segment.start.toFloat().valueOr(0),
                    .end = segment.end.toFloat().valueOr(0),
                    .text = std::move(segment.text),
                    .completed = segment.completed,
                };
            }) | ranges::to_vector);
        } catch (const AException& e) {
            ALogger::err(LOG_TAG) << "Can't process packet: " << e;
        }
    });
    connect(mWebsocket->websocketClosed, [this] {
        emit closed;
    });


    ACurlMulti::global() << mWebsocket;
}

void WhisperLive::writePcm16khz(std::vector<float> samples) {
    ACurlMulti::global().getThread()->enqueue([websocket = mWebsocket, samples = std::move(samples)] {
        websocket->writeBinary(AByteBufferView(reinterpret_cast<const char*>(samples.data()), samples.size() * sizeof(float)));
    });
}

