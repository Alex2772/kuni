#pragma once
#include "AUI/Common/AString.h"
#include "AUI/Thread/AFuture.h"
#include "AUI/Image/AImage.h"
#include "IStableDiffusionClient.h"
#include "IOpenAIChat.h"

/**
 * ImageGenerator is responsible for generating high-quality images based on freeform descriptions.
 * It uses a StableDiffusionClient for image generation and an IOpenAIChat (with vision) for
 * prompt engineering and iterative image assessment.
 */
class ImageGenerator {
public:
    ImageGenerator(_<IStableDiffusionClient> sdClient, _<IOpenAIChat> openAI, IOpenAIChat::Params chatParams)
        : mSdClient(std::move(sdClient)), mOpenAI(std::move(openAI)), mChatParams(std::move(chatParams)) {}

    struct GalleryImage {
        _<AImage> image;
        APath path;
    };

    /**
     * Generates an image from a description.
     * Uses IOpenAIChat to transform the description into an SD-optimized prompt,
     * pulls character details from KuniCharacter, and iteratively refines the prompt
     * based on vision-based assessment of the generated images.
     */
    AFuture<GalleryImage> generate(AString description);

private:
    _<IStableDiffusionClient> mSdClient;
    _<IOpenAIChat> mOpenAI;
    IOpenAIChat::Params mChatParams;

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
