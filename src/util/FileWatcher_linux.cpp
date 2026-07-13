#ifndef __APPLE__
#include "FileWatcher.h"
#include <AUI/Platform/linux/AINotifyFileWatcher.h>

namespace util {
    struct FileWatcher::Impl {
        _<AINotifyFileWatcher> watcher = _new<AINotifyFileWatcher>();
    };

    FileWatcher::FileWatcher() : mImpl(std::make_unique<Impl>()) {
        AObject::connect(mImpl->watcher->fired, this, [this](const AINotifyFileWatcher::Event& e) {
            emit fired(Event{e.watchDescriptor});
        });
    }

    FileWatcher::~FileWatcher() = default;

    int FileWatcher::addWatch(const APath& path, Mask mask) {
        AINotifyFileWatcher::Mask targetMask = AINotifyFileWatcher::Mask::MODIFY;
        if (mask == Mask::MODIFY) {
            targetMask = AINotifyFileWatcher::Mask::MODIFY;
        }
        return mImpl->watcher->addWatch(path, targetMask);
    }
}
#endif
