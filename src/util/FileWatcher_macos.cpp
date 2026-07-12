#ifdef __APPLE__
#include "FileWatcher.h"
#include <AUI/Thread/AThread.h>
#include <CoreServices/CoreServices.h>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>
#include <filesystem>

namespace util {
    struct FileWatcher::Impl {
        int nextWd = 1;
        std::map<int, AString> watches;
        std::mutex mutex;

        FSEventStreamRef stream = nullptr;
        CFRunLoopRef runLoop = nullptr;
        _<AThread> thread;
        std::atomic<bool> ready{false};
        FileWatcher* parent = nullptr;

        ~Impl() {
            if (runLoop) CFRunLoopStop(runLoop);
            if (stream) {
                FSEventStreamStop(stream);
                FSEventStreamInvalidate(stream);
                FSEventStreamRelease(stream);
            }
        }

        void rebuildStream() {
            std::map<int, AString> localWatches;
            {
                std::lock_guard<std::mutex> lock(mutex);
                localWatches = watches;
            }

            if (stream) {
                FSEventStreamStop(stream);
                FSEventStreamInvalidate(stream);
                FSEventStreamRelease(stream);
                stream = nullptr;
            }
            if (localWatches.empty()) return;

            CFMutableArrayRef pathsToWatch = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
            for (const auto& pair : localWatches) {
                std::filesystem::path p(pair.second.toStdString());
                std::string parentDir = p.parent_path().string();
                
                CFStringRef cfPath = CFStringCreateWithCString(NULL, parentDir.c_str(), kCFStringEncodingUTF8);
                CFArrayAppendValue(pathsToWatch, cfPath);
                CFRelease(cfPath);
            }

            FSEventStreamContext context = {0, this, NULL, NULL, NULL};
            stream = FSEventStreamCreate(NULL,
                &Impl::callback,
                &context,
                pathsToWatch,
                kFSEventStreamEventIdSinceNow,
                0.1,
                kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer
            );

            CFRelease(pathsToWatch);
            if (!stream) return;

            FSEventStreamScheduleWithRunLoop(stream, runLoop, kCFRunLoopDefaultMode);
            FSEventStreamStart(stream);
        }

        static void callback(ConstFSEventStreamRef streamRef,
                             void *clientCallBackInfo,
                             size_t numEvents,
                             void *eventPaths,
                             const FSEventStreamEventFlags eventFlags[],
                             const FSEventStreamEventId eventIds[]) {
            auto* self = static_cast<Impl*>(clientCallBackInfo);
            char** paths = static_cast<char**>(eventPaths);

            std::map<int, AString> localWatches;
            {
                std::lock_guard<std::mutex> lock(self->mutex);
                localWatches = self->watches;
            }

            for (size_t i = 0; i < numEvents; i++) {
                AString path = paths[i];
                if (eventFlags[i] & (kFSEventStreamEventFlagItemModified | kFSEventStreamEventFlagItemCreated | kFSEventStreamEventFlagItemRenamed)) {
                    for (const auto& pair : localWatches) {
                        if (path == pair.second) {
                            (*self->parent) ^ self->parent->fired(Event{pair.first});
                        }
                    }
                }
            }
        }
    };

    FileWatcher::FileWatcher() : mImpl(std::make_unique<Impl>()) {
        mImpl->parent = this;
        mImpl->thread = _new<AThread>([this] {
            mImpl->runLoop = CFRunLoopGetCurrent();
            
            CFRunLoopSourceContext sourceContext = {0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
            CFRunLoopSourceRef source = CFRunLoopSourceCreate(NULL, 0, &sourceContext);
            CFRunLoopAddSource(mImpl->runLoop, source, kCFRunLoopDefaultMode);
            CFRelease(source);
            
            mImpl->ready = true;
            CFRunLoopRun();
        });
        
        mImpl->thread->start();
        
        while (!mImpl->ready) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    FileWatcher::~FileWatcher() = default;

    int FileWatcher::addWatch(const APath& path, Mask mask) {
        int wd;
        {
            std::lock_guard<std::mutex> lock(mImpl->mutex);
            wd = mImpl->nextWd++;
            mImpl->watches[wd] = path.absolute();
        }
        mImpl->rebuildStream();
        return wd;
    }
}
#endif
