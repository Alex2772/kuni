//
// Created by alex2772 on 5/9/26.
//

#include "record_audio.h"

#include "VoiceGenerator.h"
#include "util/secrets.h"

OpenAITools::Tool tools::recordAudio() {
    return {
        .name = "record_audio",
        .description = "Records a new voice message and stores it in Kuni's voice gallery. This is useful for expressing emotions in a more direct way."
                        "The result of this tool is a filename. The filename can then be sent to someone else using #send_telegram_message.",
        .parameters = {
            .properties = {
                {"audio_desc", {
                    .type = "string",
                    .description = "Specifies the message Kuni would like to say. This is a TTS prompt, so the text will be converted directly into speech. Do NOT include instructions for the voice message in this field. Instead, write EXACTLY what you would say in a #send_telegram_message call. The description only has to include what the user will hear in the final voice message.",
                }},
            },
            .required = {"audio_desc"},
        },
        .handler = [](OpenAITools::Ctx ctx) -> AFuture<AString> {
            auto audioDesc = ctx.args["audio_desc"].asStringOpt().valueOrException("audio_desc is required");
            if (audioDesc.trim().empty()) {
                throw AException("audio_desc must not be empty");
            }

            // really dirty fix: hit Kuni with an exception if it tries to say an introduction in a voice note
            if (audioDesc.contains("voice") || audioDesc.contains("tone") || audioDesc.contains("Kuni")
                || audioDesc.contains("голосом") || audioDesc.contains("тоном")) {
                throw AException("Skip introductions in voice message. Instead, send the message content directly. For example, if you want to say \"Kuni says hello in a playful tone\" in a voice message, just send \"hello\".");
            }

            auto ttsApiKey = util::secrets()["elevenlabs"]["api_key"].as_string();
            AString voiceId = "pPdl9cQBQq4p6mRkZy2Z";
            if (util::secrets()["elevenlabs"].contains("voice_id")) {
                voiceId = util::secrets()["elevenlabs"]["voice_id"].as_string();
            }
            VoiceGenerator generator(ttsApiKey, voiceId);
            auto voiceMessage = co_await generator.generate(audioDesc, "ru", 1.2);

            co_return "Filename: {}"_format(voiceMessage.path.filename());
        },
    };
}
