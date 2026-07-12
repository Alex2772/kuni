#pragma once
#include <AUI/Common/AObject.h>
#include <AUI/Common/ASignal.h>
#include <AUI/IO/APath.h>
#include <memory>

namespace util {
    class FileWatcher : public AObject {
    public:
        enum class Mask { MODIFY = 1 };
        struct Event { int watchDescriptor; };

        FileWatcher();
        ~FileWatcher() override;

        int addWatch(const APath& path, Mask mask);
        emits<Event> fired;

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}
