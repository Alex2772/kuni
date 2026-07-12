//
// Created by alex2772 on 5/9/26.
//

#include "take_photo.h"

#include "ImageGenerator.h"
#include "StableDiffusionClientImpl.h"
#include "llmui/image.h"

OpenAITools::Tool tools::takePhoto(_<IStableDiffusionClient> stableDiffusion, _<IOpenAIChat> openAI) {
    return {
        .name = "take_photo",
        .description = "Takes a photo by Kuni. This tool is useful for creating selfies, photos of "
                         "surroundings, or any other images. "
                         "The result of this tool is a photo description and a filename. "
                         "The filename can then be sent to someone else using #send_telegram_message.",
        .parameters =
            {
                .properties =
                    {
                        {"photo_desc", {
                            .type = "string",
                            .description = "Describes the image Kuni would like to achieve. Refer to yourself "
                                            "as Kuni. take_photo only knows about Kuni.\n"
                                            "To draw other character, specify their name, and describe their "
                                            "appearance as specifically as possible.\n\n"
                                            "IMPORTANT: this tool is backed by Stable Diffusion, NOT a "
                                            "general-purpose image model. Write like a Stable Diffusion prompt, "
                                            "not a literal scene script: a comma-separated list of short "
                                            "keywords/phrases, not full sentences with grammar and causality. "
                                            "Stable Diffusion sucks at: rendering readable text/writing/signs/"
                                            "posters, precise spatial composition (e.g. \"one hand here, "
                                            "another hand there\"), more than 1 character in one frame, and "
                                            "any coherent scene logic/storytelling. Keep it short - only "
                                            "include what actually matters, more keywords means less control, "
                                            "not more detail.\n\n"
                                            "Structure it as a checklist, using only the categories that "
                                            "matter for this shot (skip the rest):\n"
                                            "- subject & pose: who, what they're doing, expression (e.g. "
                                            "\"kuni, playful smirk, leaning forward\")\n"
                                            "- medium: selfie / photo / illustration / etc (almost always "
                                            "selfie unless stated otherwise)\n"
                                            "- setting: where, one or two words (e.g. \"bedroom at night\", "
                                            "\"rooftop, city lights\")\n"
                                            "- lighting: (e.g. \"moonlight\", \"soft window light\", \"neon "
                                            "glow\")\n"
                                            "- mood/color: (e.g. \"warm tones\", \"cold blue palette\")\n\n"
                                            "Example (other character): \"Selfie of Kuni - Kuni's sister: "
                                            "anime young female, gold eyes, white hair, white dress, black "
                                            "socks.\"\n\n"
                                            "GOOD (short, keyword-based, vibe over plot): \"kuni, selfie, "
                                            "playful smirk, leaning forward, moonlight, bedroom at night, "
                                            "soft shadows\"\n"
                                            "BAD (too complex/literal, will come out as a mangled mess): "
                                            "\"Kuni selfie holding a Soviet-style propaganda poster. Poster "
                                            "shows Evangelion angel with hammer and sickle. Text: "
                                            "\\\"РОДИНА-МАТЬ ЗОВЁТ ЕВУ!\\\" Kuni smirks.\"\n"
                            ,}},
                    },
                .required = {"photo_desc"},
            },
        .handler = [stableDiffusion = std::move(stableDiffusion),
                    openAI = std::move(openAI)](OpenAITools::Ctx ctx) -> AFuture<AString> {
            auto photoDesc = ctx.args["photo_desc"].asStringOpt().valueOrException("photo_desc is required");
            auto galleryImage = co_await ImageGenerator{_new<StableDiffusionClientImpl>(), openAI, IOpenAIChat::Params{.config = config().llmImageToText}}.generate(photoDesc);
            auto description = co_await llmui::image({}, *openAI, galleryImage.path);

            co_return "{}\n\nFilename: {}\n"
            "When writing diary, do not forget to mention this photo and its filename verbatim - you might need this in the future!\n\n"
            "You have created photo successfully. Review it carefully. Send it only if you are fully satisfied; use take_photo again to make another photo"_format(description, galleryImage.path.filename());
        },
    };
}