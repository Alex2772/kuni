#pragma once
#include "AUI/Common/AString.h"
#include "AUI/Thread/AFuture.h"
#include "AUI/Image/AImage.h"
#include "StableDiffusionClient.h"
#include "OpenAIChat.h"

/**
 * ImageGenerator is responsible for generating high-quality images based on freeform descriptions.
 * It uses a StableDiffusionClient for image generation and an OpenAIChat (with vision) for
 * prompt engineering and iterative image assessment.
 */
class ImageGenerator {
public:
    ImageGenerator(StableDiffusionClient sdClient, OpenAIChat chatClient)
        : mSdClient(std::move(sdClient)), mChatClient(std::move(chatClient)) {}

    /**
     * Generates an image from a description.
     * Uses OpenAIChat to transform the description into an SD-optimized prompt,
     * pulls character details from KuniCharacter, and iteratively refines the prompt
     * based on vision-based assessment of the generated images.
     */
    AFuture<_<AImage>> generate(AString description);

private:
    StableDiffusionClient mSdClient;
    OpenAIChat mChatClient;

    struct PromptPair {
        AString positive;
        AString negative;
    };

    struct AssessmentResult {
        bool satisfied;
        AString feedback;
    };

    AFuture<> engineerPrompt(PromptPair& out, const AString& description, const AString& appearancePrompt, const AString& feedback = "");
    AFuture<AssessmentResult> assessImage(const AImage& image, const AString& description);
};
