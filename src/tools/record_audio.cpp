//
// Created by alex2772 on 5/9/26.
//

#include "record_audio.h"

#include "prompts.h"
#include "speech/VoiceGenerator.h"

OpenAITools::Tool tools::recordAudio() {
    return {
        .name = "record_audio",
        .description = "Records a new voice message and stores it in Kuni's voice gallery. This is useful for expressing emotions in a more direct way."
                        "The result of this tool is a filename. The filename can then be sent to someone else using #send_telegram_message.",
        .parameters = {
            .properties = {
                {"speech", {
                    .type = "string",
                    .description = prompts().recordAudioSpeech,
                }},
            },
            .required = {"speech"},
        },
        .handler = [](OpenAITools::Ctx ctx) -> AFuture<AString> {
            auto audioDesc = ctx.args["speech"].asStringOpt().valueOrException("speech is required");
            if (audioDesc.trim().empty()) {
                throw AException("speech must not be empty");
            }

            // really dirty fix: hit Kuni with an exception if it tries to say an introduction in a voice note
            if (audioDesc.contains("voice") || audioDesc.contains("tone") || audioDesc.contains("Kuni")
                || audioDesc.contains("голосом") || audioDesc.contains("тоном")) {
                throw AException("Skip introductions in voice message. Instead, send the message content directly. For example, if you want to say \"Kuni says hello in a playful tone\" in a voice message, just send \"hello\".");
            }

            VoiceGenerator generator;
            auto voiceMessage = co_await generator.generate(audioDesc, "ru", 1.2);

            co_return "Filename: {}"_format(voiceMessage.path.filename());
        },
    };
}
