#pragma once
#include "AUI/Common/AString.h"
#include "AUI/Common/AVector.h"
#include "AUI/Common/AMap.h"
#include "AUI/Json/AJson.h"
#include "AUI/Thread/AFuture.h"
#include "AUI/Image/AImage.h"
#include "Endpoint.h"

/**
 * @brief Minimal client for the ComfyUI HTTP/WebSocket API.
 * @details
 * ComfyUI exposes a native HTTP and WebSocket interface (by default at `http://127.0.0.1:8188/`) that lets us
 * submit exported "API format" workflow graphs (`/prompt`), track their execution over a websocket (`/ws`) and
 * fetch back the resulting images (`/history/{prompt_id}` + `/view`).
 *
 * @see https://docs.comfy.org/development/comfyui-server/api-examples
 */
namespace comfy {

/**
 * @brief Result of comfy::prompt: images produced by the workflow's output (SaveImage/PreviewImage-like) nodes.
 */
struct PromptResult {
    /**
     * @brief Decoded output images, in the order they were reported by /history.
     */
    AVector<_<AImage>> images;

    /**
     * @brief Raw `/history/{prompt_id}` response, in case the caller needs something beyond images.
     */
    AJson history;
};

/**
 * @brief Uploads a workflow API graph, waits for it to complete and downloads the resulting images.
 * @param workflow "API format" workflow graph (i.e. the JSON exported via "Save (API format)" in ComfyUI, or
 *        built programmatically), keyed by node id.
 * @details
 * Internally this:
 * 1. opens a websocket to `${endpoint}/ws?clientId=...`;
 * 2. POSTs the graph together with the client id to `${endpoint}/prompt`;
 * 3. waits for an `executing` message with `node == null` for our `prompt_id` (i.e. the whole graph is done);
 * 4. fetches `${endpoint}/history/{prompt_id}` and downloads every reported output image via `${endpoint}/view`.
 */
AFuture<PromptResult> prompt(AJson workflow);

/**
 * @brief Uploads an image to ComfyUI's input folder so it can be referenced by a `LoadImage`-like node.
 * @param image image to upload.
 * @param filename desired filename (passed as-is to ComfyUI; it may rename on collision).
 * @return the filename ComfyUI actually stored the image under (i.e. `name` field of `/upload/image` response).
 * @see https://docs.comfy.org/development/comfyui-server/api-examples
 */
AFuture<AString> uploadImage(const AImage& image, AString filename);

AFuture<> unload();

}   // namespace comfy
