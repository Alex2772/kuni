#pragma once

#include <AUI/Thread/AEventLoop.h>
#include <AUI/Thread/AAsyncHolder.h>

namespace util {
/**
 * @brief Unlike AFuture<T>::get(), runs an event loop to give other threads an opportunity to enqueue.
 * @tparam T
 * @param future
 * @return
 */
template <typename T>
static T await_synchronously(const AFuture<T>& future) {
    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    AAsyncHolder async;

    async << future;

    while (!async.empty()) {
        loop.iteration();
    }
    AThread::processMessages();
    return *future;
}
}