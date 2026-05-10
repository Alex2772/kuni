#pragma once
#include "IStableDiffusionClient.h"

/**
 * @brief Concrete implementation of IStableDiffusionClient that communicates
 *        with a Stable Diffusion WebUI API endpoint.
 */
struct StableDiffusionClientImpl: IStableDiffusionClient {
    Endpoint endpoint = config::ENDPOINT_SD;

    AFuture<Txt2ImgResponse> txt2img(const Txt2ImgRequest& request) override;
    AFuture<> unloadCheckpoint() override;
};
