#include "ImageGenerator.h"

#include <random>
#include <range/v3/algorithm/count_if.hpp>

#include "KuniCharacter.h"
#include "AUI/Logging/ALogger.h"
#include "AUI/Util/kAUI.h"
#include <range/v3/view/transform.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/reverse.hpp>

#include "OpenAIChat.h"
#include "AUI/Image/png/PngImageLoader.h"
#include "AUI/IO/AFileInputStream.h"

static constexpr auto LOG_TAG = "ImageGenerator";
static constexpr auto TRIAL_COUNT = 10;

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
    AString appearancePrompt = kuni_character::getAppearancePrompt();
    int trialIndex = 0;

    naxyi:
    ALogger::info(LOG_TAG) << "Engineering initial prompt for: " << description;
    PromptPair currentPrompt {
        .positive = "",
        .negative = "(text:2), (signature:2), raw photo",
    };
    co_await engineerPrompt(currentPrompt, description, appearancePrompt);
    AString firstFeedback;

    AString descriptionWithAppearance = "<character name=\"Kuni\" canonical_description>\n{}\n</character canonical_description>\n<user_description overrides_canonical=\"true\">\n{}\n</user_description overrides_canonical=\"true\">"_format(appearancePrompt, description);

    while (trialIndex <= TRIAL_COUNT) {
        try {
            ++trialIndex;
            static std::default_random_engine ge(std::time(nullptr));
            {
                ALogger::info(LOG_TAG) << "Iteration " << trialIndex << " with prompt:\npositive=" << currentPrompt.positive << "\n\nnegative=" << currentPrompt.negative;

                StableDiffusionClient::Txt2ImgResponse response;
                try
                {
                    response = co_await mSdClient.txt2img({
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
                co_await engineerPrompt(currentPrompt, description, appearancePrompt, assessment.feedback);
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
    auto safeDescription = description;
    for (const auto& word : LOL_WHAT) {
        safeDescription.replaceAll(word, "");
    }
    OpenAIChat chat = mChatClient;
    chat.maxTokens = 3000;
    chat.systemPrompt = R"(
You are an expert Stable Diffusion prompt engineer.
Your task is to transform a freeform description into a high-quality, descriptive Stable Diffusion prompt.
You must also integrate the provided character appearance details.

Guidelines:
- Use descriptive keywords, artist names, and technical terms (e.g., "hyperrealistic", "8k", "masterpiece").
- Ensure the character's appearance matches the provided appearance prompt. Appearance prompt includes both freeform
  description and stable-diffusion-optimized prompt. Base your prompt on the character's stable-diffusion prompt,
  preserving original aesthetics of the character, avoid altering original character design.
- Who made the image and how? Almost always it would be selfie, unless description explicitly specifies a photographer.
- If previous prompt iteration were provided, adjust them according to feedback and make sure it still satisfies
  original character design and desired photo description.

# Characters

<character name="Kuni">
{}
</character name="Kuni">

# Desired photo description

```
{}
```

# Output formatting

Respond in JSON object format with the following fields:

- "positivePrompt": string, positive prompt
- "negativePrompt": string, negative prompt

)"_format(appearancePrompt, safeDescription);

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

        message += R"(

<instructions>
When improving the prompt:
- Prefer Stable Diffusion weighting syntax like (term:1.2) or (phrase:1.5).
- Do not make the prompt longer just to improve emphasis.
- If the prompt is too long, shorten it by removing filler words.
- Only add new words if the image is missing a critical concept.
- Keep the final prompt short, structured, and friendly for Stable Diffusion.
- Do not alter original character design
- "positivePrompt": string, a slightly modified version of the current positive prompt to fix the issues.
- "negativePrompt": string, a slightly modified version of the current negative prompt to fix the issues.

Positive prompt is what to include to the image.

Negative prompt is what to avoid in the image.
</instructions>
    )";

        message += "\nGenerate SD prompt:";
        return AVector<OpenAIChat::Message>{
            OpenAIChat::Message{
                .role = OpenAIChat::Message::Role::USER,
                .content = message,
            }
        };
    }();
    auto response = co_await chat.chat(messages);
    naxyi:
    if (response.choices.empty()) {
        throw AException("OpenAI returned no choices for initial prompt engineering");
    }
    auto content = response.choices[0].message.content;
    auto json = parseResponse(content);
    out = {
        .positive = json["positivePrompt"].asString(),
        .negative = json["negativePrompt"].asString(),
    };

    for (const auto&[name, prompt] : std::array {std::make_pair("positive", &out.positive), std::make_pair("negative", &out.negative) }) {
        prompt->replaceAll(") ", "), "); // add commas
        auto wordCount = ranges::count_if(*prompt, [](char c ){ return c == ' '; });
        if (wordCount > 60) {
            // long prompts to stable diffusion are generally distorting the character base design.
            if (messages.size() > 3) {
                throw AException("adjusted {} prompt is too long."_format(name));
            }
            messages << OpenAIChat::Message{
                .role = OpenAIChat::Message::Role::USER,
                .content = "Adjusted {} prompt is too long. Shorten it to 50 words or less.; restructure or adjust word (weights:1.5) instead"_format(name)
            };
            response = co_await chat.chat(messages);
            ALogger::warn(LOG_TAG) << "Adjusted {} prompt is too long. Shortening..."_format(name);
            goto naxyi;
        }
    }

    for (const auto& badWord : LOL_WHAT) {
        if (description.contains(badWord)) {
            out.positive += " ";
            out.positive += badWord;
        }
    }


    co_return;
}

AFuture<ImageGenerator::AssessmentResult> ImageGenerator::assessImage(const AImage& image, const AString& description) {
    OpenAIChat chat = mChatClient;
    chat.maxTokens = 3000;
    // Note: mChatClient.config should ideally be a vision-capable model.
    chat.systemPrompt = R"(
You are an extremely strict image critic and Stable Diffusion quality gate.

You will be shown an image generated from a user description and a character appearance prompt.
Your job is to decide whether the image is an almost perfect match.
Be exceptionally picky: if there is any noticeable flaw, ambiguity, inconsistency, or implausible composition, reject it.

Reject the image if ANY of the following are true:
- Any body part is malformed, missing, duplicated, fused, unnaturally small/large, or placed incorrectly.
- Hands, fingers, arms, legs, feet, eyes, face, teeth, hair, or clothing look even slightly wrong, distorted, or inconsistent.
- The character identity or appearance does not closely match the provided description.
- The pose, physics, or composition is unreasonable or unnatural.
- The character appears to be floating, flying, suspended, falling incorrectly, or otherwise violating expected
  gravity/scene logic unless explicitly requested.
- The scene contains awkward anatomy, weird perspective, broken proportions, or AI-like artifacts.
- The image has any visible quality issue: blur, low detail, weird textures, melting, extra limbs, duplicate objects,
  warped edges, bad lighting, or inconsistent shadows.
- The image only partially satisfies the description.
- You are uncertain whether the image is correct.
- Canonical character design was not preserved.
- Canonical character description includes all known facts about the character; including those that are not directly
  related to the composition requested by the user.
- If the user's description overrides a canonical detail, the image must follow the description, not the canonical
  detail. Canonical design is the fallback only when the description does not specify an alternative.
- "explicit nudity", unless asked.

Important rule:
- If there is any reasonable doubt, set "satisfied" to false.
- Use canonical character design as the default baseline, but let the user's description override any conflicting details.
- If canonical says one thing and description says another, judge the image against the description.
- Only set "satisfied" to true if the image is excellent, coherent, anatomically correct, compositionally plausible, and
  closely matches the description.

Output your assessment in JSON format with the following fields:
- "satisfied": boolean, true if the image is high quality and matches the description.
- "feedback": string, explaining what's wrong if not satisfied.

{}
)";
    chat.systemPrompt = chat.systemPrompt.format(description);

    AVector<OpenAIChat::Message> messages = {
        OpenAIChat::Message{
            .role = OpenAIChat::Message::Role::USER,
            .content = "Assess this image: " + OpenAIChat::embedImage(image)
        }
    };
    auto response = co_await chat.chat(messages);

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
        ALogger::err(LOG_TAG) << "Failed to parse assessment JSON: " << e << "\nContent: " << responseContent;
        // Fallback: assume satisfied if parsing fails to avoid infinite loops, but log error
        co_return AssessmentResult{.satisfied = false, .feedback = "" };
    }
}
