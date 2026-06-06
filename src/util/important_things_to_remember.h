#pragma once

#include "format_past_hours.h"

#include <AppBase.h>
#include <IOpenAIChat.h>
#include <OpenAIChatImpl.h>

namespace util {
AFuture<AString> importantThingsToRemember(IOpenAIChat& openAI, AVector<IOpenAIChat::Message> context, AStringView previousWorkingMemory) {
    using namespace std::chrono_literals;

    AString prompt = "What are important things in timespan {} (3 days) you should remember?\n"_format(formatPastHours(24h * 3));
    prompt += "Do not attempt to make tool_calls or #ask. Your job is to summarize your current tasks and "
              "revisit tasks from previous session.\n";
    if (!previousWorkingMemory.empty()) {
        prompt += "\nHere is the PREVIOUS <things_to_remember> from the last session. "
                  "You MUST preserve ALL items verbatim from it, except:\n"
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
              "- Kuni's current emotional state: sad, arousal, betrayed, grief, sorrow, pensiveness, anger, annoyed, "
              "  fear, amazement, surprise, distracted, love, trust, accepted, joy, serenity, embarrassed, proud, "
              "  envious, jealous, grateful, disappointed, hopeful, lonely, nostalgic, relieved, resentful\n"
              "- Kuni's current physical state: energetic, tired, exhausted, sleepy, restless, tense, relaxed, "
              "  hungry, thirsty, sore, dizzy, nauseous, feverish, cold, warm, comfortable, uncomfortable, "
              "  in_pain, injured, healthy, sick, weak, stiff, trembling, numb, cold, warm, wet, dry\n"
              "- other important details\n"
              "In your response, you must include previous <things_to_remember></things_to_remember>. DO NOT ALTER "
              "THEIR DESCRIPTION, thought you can expand them. You should not include older, "
              "outdated items and completed tasks.\n"
              "You must write briefly (100-500 words), structurize output, INCLUDE DATES.\n"
              "Each item MUST have a \"last updated\" date. "
              "Example format:\n"
              "- Напомнить Алексею про оплату хостинга до 15 мая — последнее обновление: Apr 28\n"
              "- Жду ответ от Марии по поводу встречи в пятницу — последнее обновление: Apr 29\n"
              "- Обещала скинуть рецепт пасты Коду — последнее обновление: Apr 27\n"
              "- Проверить статус заказа на Ozon — последнее обновление: Apr 25\n";

    context << IOpenAIChat::Message {
        .role = IOpenAIChat::Message::Role::USER,
        .content = std::move(prompt),
    };
    for (;;) {
        auto content = (co_await openAI.chat({
            .systemPrompt = AppBase::getSystemPrompt(),
            .config = config::ENDPOINT_MAIN,
        }, context)).choices.at(0).message.content;
        if (content.contains("tool_calls") || content.contains("ask")) {
            // deepseek bug - attempts to use DSML to make a tool call.
            continue;
        }
        co_return content;
    }

}
}