#pragma once
//
// Shared mock for IOpenAIChat used across unit and integration tests.
//

#include "IOpenAIChat.h"
#include <gmock/gmock.h>

class OpenAIMock : public IOpenAIChat {
public:
    MOCK_METHOD(_<StreamingResponse>, chatStreaming, (Params params, IOpenAIChat::Session messages), (override));
    MOCK_METHOD(AFuture<std::valarray<double>>, embedding, (Params params, AString input), (override));
    MOCK_METHOD(AFuture<AudioTranscription>, transcribeAudio, (AByteBufferView audio, AStringView format), (override));
};
