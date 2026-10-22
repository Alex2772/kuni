#pragma once
#include "AUI/Common/AObject.h"
#include "AUI/Common/AString.h"
#include "AUI/Curl/AWebsocket.h"

class WhisperLive: public AObject {
public:
    struct Config {
        AString endpoint = "ws://localhost:9001";
        AString language = "en";
        AString model = "base";
    };
    WhisperLive(Config config);

    void writePcm16khz(std::vector<float> samples);

    bool connected() const noexcept {
        return mConnected;
    }

    struct Segment {
        float start{};
        float end{};
        AString text;
        bool completed{};
    };

    emits<AVector<Segment>> update;
    emits<> closed;

private:
    AArc<AWebsocket> mWebsocket;
    bool mConnected = false;
};
