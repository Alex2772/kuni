//
// Created by alex2772 on 5/14/26.
//

#include "OpenAIChatMeasurable.h"

_<IOpenAIChat::StreamingResponse> OpenAIChatMeasurable::chatStreaming(Params params, IOpenAIChat::Session messages) {
    auto result = mWrapped->chatStreaming(params, std::move(messages));
    result->completed.onSuccess([this, self = weak_from_this(), result = result.weak(), model = params.config.model] {
        auto lock1 = self.lock();
        if (!lock1) return;
        auto lock2 = result.lock();
        if (!lock2) return;
        AThread::main()->enqueue([this, lock1, usage = std::move(lock2->response->usage), model] {
            emit responseMetrics({
                .model = model,
                .usage = std::move(usage),
            });
        });
    });
    return result;
}

AFuture<std::valarray<double>> OpenAIChatMeasurable::embedding(Params params, AString input) {
    return mWrapped->embedding(std::move(params), std::move(input));
}
