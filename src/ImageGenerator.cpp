#include "ImageGenerator.h"

#include <random>

#include "KuniCharacter.h"
#include "AUI/Logging/ALogger.h"
#include "AUI/Util/kAUI.h"
#include <range/v3/view/transform.hpp>
#include <range/v3/range/conversion.hpp>

#include "AUI/Image/png/PngImageLoader.h"

static constexpr auto LOG_TAG = "ImageGenerator";

AFuture<_<AImage>> ImageGenerator::generate(AString description) {
    AString appearancePrompt = kuni_character::getAppearancePrompt();
    int trialIndex = 0;

    naxyi:
    ALogger::info(LOG_TAG) << "Engineering initial prompt for: " << description;
    PromptPair currentPrompt {
        .positive = co_await engineerInitialPrompt(description, appearancePrompt),
        .negative = "text, signature",
    };

    _<AImage> lastImage;
    for (; trialIndex < 20; ++trialIndex) {
        ALogger::info(LOG_TAG) << "Iteration " << trialIndex + 1 << " with prompt:\npositive=" << currentPrompt.positive << "\n\nnegative=" << currentPrompt.negative;

        static std::default_random_engine ge(std::time(nullptr));
        auto response = co_await mSdClient.txt2img({
            .prompt = currentPrompt.positive,
            .negative_prompt = currentPrompt.negative,
            .steps =  30,
            .cfg_scale = std::uniform_real_distribution<>(1.0, 5.0)(ge),
            .width = std::uniform_int_distribution<>(768, 1024)(ge),
            .height = std::uniform_int_distribution<>(768, 1024)(ge),
        });
        if (response.images.empty()) {
            throw AException("Stable Diffusion returned no images");
        }
        lastImage = response.images[0];
        PngImageLoader::save(AFileOutputStream{ "image_generator_tmp.png" }, *lastImage);

        ALogger::info(LOG_TAG) << "Assessing image...";
        auto assessment = co_await assessImage(*lastImage, description, currentPrompt);

        if (assessment.satisfied) {
            ALogger::info(LOG_TAG) << "Satisfied with the result. " << assessment.feedback;
            auto dst = APath("data/gallery/{}.png"_format(std::chrono::system_clock::now()));
            dst.parent().makeDirs();
            PngImageLoader::save(AFileOutputStream{ dst }, *lastImage);
            co_return lastImage;
        }

        ALogger::info(LOG_TAG) << "Not satisfied. Feedback: " << assessment.feedback;
        currentPrompt = std::move(assessment.adjustedPrompts);

        if ((trialIndex + 1) % 4 == 0) {
            ALogger::info(LOG_TAG) << "Last trial failed. Retrying with different prompt...";
            goto naxyi;
        }
    }

    throw AException("can't find image");
}

AFuture<AString> ImageGenerator::engineerInitialPrompt(const AString& description, const AString& appearancePrompt) {
    OpenAIChat chat = mChatClient;
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
- Output ONLY the final prompt, nothing else.
)";

    AString message = "User description: {}\n\n<character name=\"Kuni\">{}</character name=\"Kuni\">\n\nGenerate SD prompt:"_format(description, appearancePrompt);
    auto response = co_await chat.chat(message);
    if (response.choices.empty()) {
        throw AException("OpenAI returned no choices for initial prompt engineering");
    }
    auto content = response.choices[0].message.content;
    while (content.startsWith(" ") || content.startsWith("\n") || content.startsWith("\r") || content.startsWith("\t")) {
        content = content.substr(1);
    }
    while (content.endsWith(" ") || content.endsWith("\n") || content.endsWith("\r") || content.endsWith("\t")) {
        content = content.substr(0, content.length() - 1);
    }
    co_return content;
}

AFuture<ImageGenerator::AssessmentResult> ImageGenerator::assessImage(const AImage& image, const AString& description, const PromptPair& currentPrompt) {
    OpenAIChat chat = mChatClient;
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

Important rule:
- If there is any reasonable doubt, set "satisfied" to false.
- Only set "satisfied" to true if the image is excellent, coherent, anatomically correct, compositionally plausible, and
  closely matches the description.

Output your assessment in JSON format with the following fields:
- "satisfied": boolean, true if the image is high quality and matches the description.
- "feedback": string, explaining what's wrong if not satisfied.
- "adjustedPositivePrompt": string, a slightly modified version of the current positive prompt to fix the issues.
- "adjustedNegativePrompt": string, a slightly modified version of the current negative prompt to fix the issues.

Positive prompt is what to include to the image.

Negative prompt is what to avoid in the image.

Stable diffusion supports increasing phrase weights by wrapping them in braces and a number: (age 20:2) means SD should
focus on `age 20` twice as much.

# Current positive prompt used

```
{}
```

# Current negative prompt used

```
{}
```

# User description

```
{}
```

When assessing the image, rely on "User description" only! Do not use positive prompts and negative prompts for
assessment! Use them only for generating new prompts!
)";
    chat.systemPrompt = chat.systemPrompt.format(currentPrompt.positive, currentPrompt.negative, description);

    AString message = "Assess this image: " + OpenAIChat::embedImage(image);
    auto response = co_await chat.chat(message);

    if (response.choices.empty()) {
        throw AException("OpenAI returned no choices for image assessment");
    }

    AString content = response.choices[0].message.content;
    // Basic JSON extraction if the model wrapped it in markdown
    if (content.contains("```json")) {
        content = content.split("```json").at(1).split("```").at(0);
    } else if (content.contains("```")) {
        content = content.split("```").at(1).split("```").at(0);
    }

    try {
        auto json = AJson::fromString(content);
        co_return AssessmentResult{
            .satisfied = json["satisfied"].asBool(),
            .feedback = json["feedback"].asString(),
            .adjustedPrompts {
                .positive = json["adjustedPositivePrompt"].asString(),
                .negative = json["adjustedNegativePrompt"].asString(),
            },
        };
    } catch (const AException& e) {
        ALogger::err(LOG_TAG) << "Failed to parse assessment JSON: " << e << "\nContent: " << content;
        // Fallback: assume satisfied if parsing fails to avoid infinite loops, but log error
        co_return AssessmentResult{.satisfied = true, .feedback = "JSON parsing failed", .adjustedPrompts = currentPrompt };
    }
}
