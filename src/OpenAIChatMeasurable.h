#pragma once
#include "IOpenAIChat.h"
#include "AUI/Common/AObject.h"

class OpenAIChatMeasurable: public AObject, public IOpenAIChat {
public:
    explicit OpenAIChatMeasurable(_unique<IOpenAIChat> wrapped) : mWrapped(std::move(wrapped)) {}

    ~OpenAIChatMeasurable() override = default;
    AFuture<Response> chat(Params params, AVector<Message> messages) override;
    _<StreamingResponse> chatStreaming(Params params, AVector<Message> messages) override;
    AFuture<std::valarray<double>> embedding(Params params, AString input) override;

    struct Metrics {
        AString model;
        Response::Usage usage;
    };

    emits<Metrics> responseMetrics;

private:
    _unique<IOpenAIChat> mWrapped;
};
