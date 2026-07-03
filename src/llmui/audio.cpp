//
// Created by alex2772 on 5/9/26.
//

#include "audio.h"
#include "config.h"
#include "IOpenAIChat.h"
#include "AUI/IO/AFileInputStream.h"

static constexpr auto LOG_TAG = "llmui";


AFuture<AString> llmui::voiceMessage(AStringView pathToVoice, IOpenAIChat& openAI) {
    ALOG_TRACE(LOG_TAG) << "voiceMessage pathToVoice=" << pathToVoice;
    if (!config().capabilityHearing) {
        co_return "";
    }
    try {
        AFileInputStream stream(pathToVoice);
        AByteBuffer audio = AByteBuffer::fromStream(stream);
        auto transcription = co_await openAI.transcribeAudio(audio, "ogg");
        co_return "<voice>\n" + transcription.text + "\n</voice>";
    } catch (const AException& e) {
        ALogger::err(LOG_TAG) << "Can't transcribe audio" << e;
        co_return "";
    }
}
