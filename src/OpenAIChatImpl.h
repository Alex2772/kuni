#pragma once
#include "IOpenAIChat.h"

/**
 * @brief Concrete implementation of IOpenAIChat that communicates with
 *        an OpenAI-compatible API endpoint (e.g., Ollama, OpenRouter).
 */
struct OpenAIChatImpl: IOpenAIChat {
    AFuture<Response> chat(Params params, AVector<Message> messages) override;
    _<StreamingResponse> chatStreaming(Params params, AVector<Message> messages) override;

    AFuture<std::valarray<double>> embedding(Params params, AString input) override;

private:
    static AJson makeQueryString(Params params, AVector<Message> messages);
};
