#include "malicious_payloads.h"
#include "IOpenAIChat.h"

void llmui::checkForMaliciousPayloads(std::string& string) {
    if (AStringView(string).contains(IOpenAIChat::EMBEDDING_TAG)) {
        goto naxyi;
    }
    return;
naxyi:
    string = "malicious";
}