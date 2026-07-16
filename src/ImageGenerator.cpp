#include "ImageGenerator.h"
#include <fmt/base.h>

#include <random>
#include <range/v3/algorithm/count_if.hpp>

#include "AUI/Logging/ALogger.h"
#include "AUI/Util/kAUI.h"
#include <range/v3/view/transform.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/reverse.hpp>

#include "IOpenAIChat.h"
#include "OpenAIChatImpl.h"
#include "prompts.h"
#include "AUI/Image/png/PngImageLoader.h"
#include "AUI/IO/AFileInputStream.h"

static constexpr auto LOG_TAG = "ImageGenerator";
static constexpr auto TRIAL_COUNT = 10;

/**
 * @brief Minimize risk of generating too young/child characters.
 * @details
 * This flag adjusts prompts the ImageGenerator won't make up children. This is a safety precaution for the instance
 * owner since CP is prohibited in many jurisdictions.
 */
static constexpr auto PREVENT_GENERATING_CHILDREN = true;

static AJson parseResponse(AString content) {
    // Basic JSON extraction if the model wrapped it in markdown
    if (content.contains("```json")) {
        content = content.split("```json").at(1).split("```").at(0);
    } else if (content.contains("```")) {
        content = content.split("```").at(1).split("```").at(0);
    }
    return AJson::fromString(content);
}

AFuture<ImageGenerator::GalleryImage> ImageGenerator::generate(AString description) {
    ALOG_TRACE(LOG_TAG) << "generate: " << description;
    int trialIndex = 0;

    naxyi:
    ALogger::info(LOG_TAG) << "Engineering initial prompt for: " << description;
    PromptPair currentPrompt {
        .positive = "",
        .negative = "(text:2), (signature:2), raw photo",
    };
    co_await engineerPrompt(currentPrompt, description, prompts().characterAppearance);
    AString firstFeedback;

    AString descriptionWithAppearance = "<character name=\"{}\" canonical_description>\n{}\n</character canonical_description>\n<user_description overrides_canonical=\"true\">\n{}\n</user_description overrides_canonical=\"true\">"_format(config().characterName, prompts().characterAppearance, description);

    ALogger::info(LOG_TAG) << "positive=" << currentPrompt.positive << "\n\nnegative=" << currentPrompt.negative;
    while (trialIndex <= TRIAL_COUNT) {
        try {
            ++trialIndex;
            static std::default_random_engine ge(std::time(nullptr));
            {
                ALogger::info(LOG_TAG) << "Iteration " << trialIndex;

                IStableDiffusionClient::Txt2ImgResponse response;
                try
                {
                    response = co_await mSdClient->txt2img({
                        .prompt = currentPrompt.positive,
                        .negative_prompt = currentPrompt.negative,
                        .steps =  30,
                        .cfg_scale = std::uniform_real_distribution<>(1.0, 5.0)(ge),
                        .width = std::uniform_int_distribution<>(768, 1400)(ge),
                        .height = std::uniform_int_distribution<>(768, 1400)(ge),
                        .enable_hr = true,
                        .hr_scale = 1.5,
                        .hr_upscaler = "Latent",
                        .hr_second_pass_steps = 10,
                        .denoising_strength = 0.7,
                    });
                } catch (const AException& e) {
                    ALogger::err(LOG_TAG) << "Stable diffusion failed:: " << e;
                    goto tryGallery;
                }
                if (response.images.empty()) {
                    throw AException("Stable Diffusion returned no images");
                }
                auto lastImage = response.images[0];
                PngImageLoader::save(AFileOutputStream{ "image_generator_tmp.png" }, *lastImage);
                //Unload SD checkpoint after generation
                try {
                    co_await mSdClient->unloadCheckpoint();
                    ALogger::info(LOG_TAG) << "Checkpoint unloaded from VRAM";
                } catch (const AException& e) {
                    ALogger::warn(LOG_TAG) << "Failed to unload checkpoint: " << e;
                }

                ALogger::info(LOG_TAG) << "Assessing image...";
                auto assessment = co_await assessImage(*lastImage, descriptionWithAppearance);

                if (assessment.satisfied) {
                    ALogger::info(LOG_TAG) << "Satisfied with the result. " << assessment.feedback;
                    auto dst = APath("data/gallery/{}.png"_format(std::chrono::system_clock::now()));
                    dst.parent().makeDirs();
                    PngImageLoader::save(AFileOutputStream{ dst }, *lastImage);
                    co_return GalleryImage{ .image = lastImage, .path = dst.absolute() };
                }
                if (firstFeedback.empty()) {
                    firstFeedback = assessment.feedback;
                }

                ALogger::info(LOG_TAG) << "Not satisfied. Feedback: " << assessment.feedback;
                // co_await engineerPrompt(currentPrompt, description, prompts().characterAppearance, assessment.feedback);
            }


            tryGallery:
            // // in case SD fails, let's try a photo from gallery.
            // auto galleryFiles = APath("data/gallery").listDir(AFileListFlags::REGULAR_FILES);
            // if (galleryFiles.empty())
            // {
            //     continue;
            // }
            // auto randomFile = galleryFiles[std::uniform_int_distribution<>(0, galleryFiles.size() - 1)(ge)];
            // ALogger::info(LOG_TAG) << "Trying to supply image from gallery: " << randomFile;
            // auto lastImage = AImage::fromBuffer(AByteBuffer::fromStream(AFileInputStream{ randomFile }));
            // auto assessment = co_await assessImage(*lastImage, descriptionWithAppearance);
            // if (assessment.satisfied) {
            //     ALogger::info(LOG_TAG) << "Satisfied with the image from gallery: " << assessment.feedback;
            //     co_return lastImage;
            // }

            if (trialIndex % size_t(std::sqrt(TRIAL_COUNT)) == 0) {
                ALogger::info(LOG_TAG) << "Last trial failed. Retrying with different prompt...";
                goto naxyi;
            }
        } catch (const AException& e) {
            ALogger::err(LOG_TAG) << "Failed to generate image: " << e;
        }
    }

    throw AException("can't find image: feedback: \"{}\"; make photo_desc shorter"_format(firstFeedback));
}

static constexpr auto LOL_WHAT = {
    "explicit nudity",
    "explicit nude",
    "explicit erotic",
    "pussy",
    "breasts",
    "nsfw",
    "genital",
    "vagina",
    "penis",
};

AFuture<> ImageGenerator::engineerPrompt(PromptPair& out, const AString& description, const AString& appearancePrompt, const AString& feedback) {
    ALOG_TRACE(LOG_TAG) << "engineerPrompt description=" << description << " appearancePrompt=" << appearancePrompt << " feedback=" << feedback;
    auto safeDescription = description;
    for (const auto& word : LOL_WHAT) {
        safeDescription.replaceAll(word, "");
    }
    auto params = mChatParams;
    params.systemPrompt = prompts().imageEngineerSystem.format(fmt::arg("CHARACTER_NAME", config().characterName), fmt::arg("CHARACTER_APPEARANCE_PROMPT", appearancePrompt), fmt::arg("SANITIZED_PHOTO_DESCRIPTION", safeDescription));

    auto messages = [&] {
        AString message;

        message += "# Previous prompt iteration \n";
        message += "Positive prompt: ";
        message += out.positive;
        message += "\n\n";
        message += "Negative prompt: ";
        message += out.negative;
        message += "\n\n";

        if (!feedback.empty()) {
            message += "# Feedback to previous prompt iteration\n";
            message += feedback;
        }

        message += "\n\n";
        message += prompts().imageEngineerInstructions;

        message += "\nGenerate SD prompt:";
        return IOpenAIChat::Session{
            IOpenAIChat::Message{
                .role = IOpenAIChat::Message::Role::USER,
                .content = message,
            }
        };
    }();
    naxyi_before:
    auto response = co_await mOpenAI->chat(params, messages);
    naxyi:
    if (response.choices.empty()) {
        throw AException("OpenAI returned no choices for initial prompt engineering");
    }
    auto content = response.choices[0].message.content;
    try {
        auto json = parseResponse(content);
        out = {
            .positive = json["positivePrompt"].asString(),
            .negative = json["negativePrompt"].asString(),
        };
    } catch (const AException& e) {
        if (e.getMessage().contains("unexpected end of json stream")) {
            goto naxyi_before;
        }
        throw;
    }

    for (const auto&[name, prompt] : std::array {std::make_pair("positive", &out.positive), std::make_pair("negative", &out.negative) }) {
        prompt->replaceAll(") ", "), "); // add commas
        auto wordCount = ranges::count_if(*prompt, [](char c ){ return c == ' '; });
        if (wordCount > 60) {
            // long prompts to stable diffusion are generally distorting the character base design.
            if (messages.size() > 3) {
                throw AException("adjusted {} prompt is too long."_format(name));
            }
            messages << IOpenAIChat::Message{
                .role = IOpenAIChat::Message::Role::USER,
                .content = "Adjusted {} prompt is too long. Shorten it to 50 words or less.; restructure or adjust word (weights:1.5) instead"_format(name)
            };
            response = co_await mOpenAI->chat(params, messages);
            goto naxyi;
        }
    }

    for (const auto& badWord : LOL_WHAT) {
        if (description.contains(badWord)) {
            out.positive += " ";
            out.positive += badWord;
        }
    }
    if constexpr (PREVENT_GENERATING_CHILDREN) {
        out.negative += " child";
    }


    co_return;
}

AFuture<ImageGenerator::AssessmentResult> ImageGenerator::assessImage(const AImage& image, const AString& description) {
    ALOG_TRACE(LOG_TAG) << "assessImage description=" << description;
    auto params = mChatParams;
    // Note: mChatParams.config should ideally be a vision-capable model.
    params.systemPrompt = prompts().imageAssessSystem + description;
    if constexpr (PREVENT_GENERATING_CHILDREN) {
        params.systemPrompt += "\nThe character(s) must not appear as child";
    }

    IOpenAIChat::Session messages = {
        IOpenAIChat::Message{
            .role = IOpenAIChat::Message::Role::USER,
            .content = "Assess this image: " + IOpenAIChat::embedImage(image)
        }
    };
    tryAgain:
    auto response = co_await mOpenAI->chat(params, messages);

    if (response.choices.empty()) {
        throw AException("OpenAI returned no choices for image assessment");
    }
    auto responseContent = response.choices[0].message.content;

    try {
        auto json = parseResponse(responseContent);
        AssessmentResult result{
            .satisfied = json["satisfied"].asBool(),
            .feedback = json["feedback"].asString(),
        };
        co_return result;
    } catch (const AException& e) {
        if (e.getMessage().contains("unexpected end of json stream")) {
            goto tryAgain;
        }
        ALogger::err(LOG_TAG) << "Failed to parse assessment JSON: " << e << "\nContent: " << responseContent;
        // Fallback: assume satisfied if parsing fails to avoid infinite loops, but log error
        co_return AssessmentResult{.satisfied = false, .feedback = "" };
    }
}
