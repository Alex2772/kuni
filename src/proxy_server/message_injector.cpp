//
// Created by alex2772 on 6/8/26.
//

#include "message_injector.h"

namespace proxy_server {

void MessageInjector::store(const AString& visibleAssistantContent, Messages hiddenMessages) {
    mStore[visibleAssistantContent] = std::move(hiddenMessages);
}

MessageInjector::Messages MessageInjector::merge(Messages messages) const {
    if (mStore.empty()) {
        return messages;
    }

    Messages result;
    result.reserve(messages.size());

    for (auto& msg : messages) {
        if (msg.role == IOpenAIChat::Message::Role::ASSISTANT && !msg.content.empty()) {
            auto it = mStore.find(msg.content);
            if (it != mStore.end()) {
                // Splice hidden messages right before this visible assistant turn.
                for (const auto& hidden : it->second) {
                    result.push_back(hidden);
                }
            }
        }
        result.push_back(std::move(msg));
    }

    return result;
}

} // namespace proxy_server

