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
                                            "as Kuni. Avoid unnecessary details. Instead of specifying complex "
                                            "composition, prefer setting vibe of the image. "
                                            "Example: \"Kuni makes playful selfie\""
                                            "take_photo only knows about Kuni.\n"
                                            "To draw other character, specify their name, and describe their\n"
                                            "appearance as specifically as possible."
                                            "Example: \"Selfie of Kuni - Kuni's sister: anime young female,"
                                            "gold eyes, white hair, white dress, black socks.\"\n"
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