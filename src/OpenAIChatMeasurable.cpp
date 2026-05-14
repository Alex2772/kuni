//
// Created by alex2772 on 5/14/26.
//

#include "OpenAIChatMeasurable.h"

AFuture<IOpenAIChat::Response> OpenAIChatMeasurable::chat(Params params, AVector<Message> messages) {
    auto result = co_await mWrapped->chat(params, std::move(messages));
    emit responseMetrics({
        .model = std::move(params.config.model),
        .usage = result.usage,
    });
    co_return result;
}

_<IOpenAIChat::StreamingResponse> OpenAIChatMeasurable::chatStreaming(Params params, AVector<Message> messages) {
    auto result = mWrapped->chatStreaming(params, std::move(messages));
    result->completed.onSuccess([this, self = weak_from_this(), result = result.weak(), model = params.config.model] {
        auto lock1 = self.lock();
        if (!lock1) return;
        auto lock2 = result.lock();
        if (!lock2) return;
        AThread::main()->enqueue([this, lock1, lock2, model] {
            emit responseMetrics({
                .model = model,
                .usage = lock2->response->usage,
            });
        });
    });
    return result;
}

AFuture<std::valarray<double>> OpenAIChatMeasurable::embedding(Params params, AString input) {
    return mWrapped->embedding(std::move(params), std::move(input));
}
