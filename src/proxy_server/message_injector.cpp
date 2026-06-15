//
// Created by alex2772 on 6/8/26.
//

#include "message_injector.h"

#include "json_fingerprint.h"

namespace proxy_server {


AVector<AJson>& MessageInjector::after(const AJson& msg) {
    return mMessagesToInsertAfter[fingerprint(msg)];
}

AJson::Array MessageInjector::merge(AJson::Array messages) const {
    if (mMessagesToInsertAfter.empty()) {
        return messages;
    }

    for (auto i = messages.begin(); i != messages.end(); ++i) {
        auto fingerprint = proxy_server::fingerprint(*i);
        auto toInsert = mMessagesToInsertAfter.find(fingerprint);
        if (toInsert == mMessagesToInsertAfter.end()) {
            continue;
        }
        if (toInsert->second.empty()) {
            continue;
        }

        i = messages.insert(std::next(i), toInsert->second.begin(), toInsert->second.end());
        i += toInsert->second.size() - 1;
    }
    return messages;
}


} // namespace proxy_server

