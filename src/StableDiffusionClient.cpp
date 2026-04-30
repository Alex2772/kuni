#include "StableDiffusionClient.h"
#include "AUI/Curl/ACurl.h"
#include "AUI/Json/Conversion.h"
#include "AUI/Logging/ALogger.h"
#include <range/v3/view/transform.hpp>
#include <range/v3/range/conversion.hpp>

#include "config.h"

static constexpr auto LOG_TAG = "StableDiffusionClient";

AJSON_FIELDS(StableDiffusionClient::Txt2ImgRequest,
             AJSON_FIELDS_ENTRY(prompt)
             AJSON_FIELDS_ENTRY(negative_prompt)
             AJSON_FIELDS_ENTRY(styles)
             AJSON_FIELDS_ENTRY(seed)
             AJSON_FIELDS_ENTRY(subseed)
             AJSON_FIELDS_ENTRY(subseed_strength)
             AJSON_FIELDS_ENTRY(seed_resize_from_h)
             AJSON_FIELDS_ENTRY(seed_resize_from_w)
             AJSON_FIELDS_ENTRY(sampler_name)
             AJSON_FIELDS_ENTRY(scheduler)
             AJSON_FIELDS_ENTRY(batch_size)
             AJSON_FIELDS_ENTRY(n_iter)
             AJSON_FIELDS_ENTRY(steps)
             AJSON_FIELDS_ENTRY(cfg_scale)
             AJSON_FIELDS_ENTRY(width)
             AJSON_FIELDS_ENTRY(height)
             AJSON_FIELDS_ENTRY(send_images)
             AJSON_FIELDS_ENTRY(override_settings)
             AJSON_FIELDS_ENTRY(save_images)
             AJSON_FIELDS_ENTRY(enable_hr)
             AJSON_FIELDS_ENTRY(hr_scale)
             AJSON_FIELDS_ENTRY(hr_upscaler)
             AJSON_FIELDS_ENTRY(hr_second_pass_steps)
             AJSON_FIELDS_ENTRY(denoising_strength))

AFuture<StableDiffusionClient::Txt2ImgResponse> StableDiffusionClient::txt2img(const Txt2ImgRequest& request) {
    ALOG_TRACE(LOG_TAG) << "txt2img";
    auto query = AJson::toString(aui::to_json(request));
    ALOG_TRACE(LOG_TAG) << "Query: " << query;

    AVector<AString> headers = {"Content-Type: application/json"};
    if (!endpoint.bearerKey.empty()) {
        headers << "Authorization: Bearer {}"_format(endpoint.bearerKey);
    }

    auto responseBody = (co_await ACurl::Builder(endpoint.baseUrl + "sdapi/v1/txt2img")
                                           .withMethod(ACurl::Method::HTTP_POST)
                                           .withHeaders(std::move(headers))
                                           .withBody(query.toStdString())
                                           .withTimeout(config::REQUEST_TIMEOUT)
                                           .runAsync())
                                          .body;
    auto response = AJson::fromBuffer(responseBody);

    ALOG_TRACE(LOG_TAG) << "Response: " << AJson::toString(response);
    StableDiffusionClient::Txt2ImgResponse res;
    res.images = response["images"].asArray() | ranges::views::transform([](const AJson& v) { return AImage::fromBuffer(AByteBuffer::fromBase64String(v.asString())); }) | ranges::to_vector;
    res.info = response["info"];
    co_return res;
}
