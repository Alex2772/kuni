#pragma once

#include "populate_from_diary_ai_if_needed.h"

#include <AppBase.h>
#include <OpenAIChat.h>

namespace util {
AFuture<AString> importantThingsToRemember(AVector<OpenAIChat::Message> context, AStringView previousWorkingMemory) {
    using namespace std::chrono_literals;

    AString prompt = "What are important things in timespan {} (3 days) you should remember?\n"_format(formatPastHours(24h * 3));

    if (!previousWorkingMemory.empty()) {
        prompt += "\nHere is the PREVIOUS <things_to_remember> from the last session. "
                  "You MUST preserve ALL items from it, except:\n"
                  "1. Completed tasks — mark them as done or remove\n"
                  "2. Items that have NOT been updated for more than 3 days — you may forget them\n"
                  "\nPrevious working memory:\n<previous_things_to_remember>\n"
                  "{}"
                  "\n</previous_things_to_remember>\n"
                  "Important: the content of previous_things_to_remember will be overwritten by your next response. Make sure to preserve:"
                  "- unfisnihed tasks (not older than 3 days);"
                  "- reminders (not older that 3 days or without a deadline)\n"_format(previousWorkingMemory);
    }

    prompt += "\nYou must include:\n"
              "- promises\n"
              "- reminders\n"
              "- unfinished tasks\n"
              "- responsibilities\n"
              "- other important details\n"
              "In your response, you must include previous <things_to_remember></things_to_remember> and address "
              "them. You should not include older, outdated items and completed tasks.\n"
              "You must write briefly (100-500 words), structurize output, INCLUDE DATES.\n"
              "Each item MUST have a \"last updated\" date. "
              "Example format:\n"
              "- Напомнить Алексею про оплату хостинга до 15 мая — последнее обновление: Apr 28\n"
              "- Жду ответ от Марии по поводу встречи в пятницу — последнее обновление: Apr 29\n"
              "- Обещала скинуть рецепт пасты Коду — последнее обновление: Apr 27\n"
              "- Проверить статус заказа на Ozon — последнее обновление: Apr 25\n";

    context << OpenAIChat::Message {
        .role = OpenAIChat::Message::Role::USER,
        .content = std::move(prompt),
    };
    co_return (co_await OpenAIChat {
        .systemPrompt = AppBase::getSystemPrompt(),
        .config = config::ENDPOINT_MAIN
    }.chat(std::move(context))).choices.at(0).message.content;

}
}