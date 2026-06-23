#pragma once
#include "IOpenAIChat.h"

/**
 * @brief Concrete implementation of IOpenAIChat that communicates with
 *        an OpenAI-compatible API endpoint (e.g., Ollama, OpenRouter).
 */
struct OpenAIChatImpl: IOpenAIChat {
    _<StreamingResponse> chatStreaming(Params params, IOpenAIChat::Session messages) override;

    AFuture<std::valarray<double>> embedding(Params params, AString input) override;

    static AFuture<AJson> makeHttpRequest(Endpoint endpoint, std::string query, std::string_view sessionId);

private:
    static AJson makeQueryString(Params params, const IOpenAIChat::Session& messages);
};
