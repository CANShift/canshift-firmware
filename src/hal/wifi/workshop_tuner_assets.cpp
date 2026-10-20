#include "app_config.h"
#if APP_WORKSHOP_ENABLED

    #include "workshop_server_internal.h"

    #include "diag/logger.h"
    #include "workshop_mode_rs.h"

    #include <esp_http_server.h>

    #include <stdint.h>
    #include <stdio.h>

namespace WorkshopServerInternal {

namespace {

constexpr char kMountPoint[] = "/spiffs";
constexpr char kIndexUri[] = "/";
constexpr char kGzipEncoding[] = "gzip";
constexpr size_t kFullPathCap = sizeof(kMountPoint) + kWorkshopTunerPathCap;

enum class AssetResult : uint8_t { Sent, Missing, Broken };

using AssetFile = HttpFile;

AssetFile openVariant(const char *uri, bool gzip) {
    char path[kWorkshopTunerPathCap];
    if (workshop_tuner_path_rs(uri, gzip, path, sizeof(path)) == 0)
        return nullptr;
    char fullPath[kFullPathCap];
    snprintf(fullPath, sizeof(fullPath), "%s%s", kMountPoint, path);
    return AssetFile(fopen(fullPath, "rb"));
}

bool setHeaders(httpd_req_t *req, const char *uri, bool gzip) {
    bool ok = httpd_resp_set_type(req, workshop_tuner_content_type_rs(uri)) == ESP_OK &&
              setSecurityHeaders(req);
    if (gzip)
        ok = ok && httpd_resp_set_hdr(req, "Content-Encoding", kGzipEncoding) == ESP_OK;
    return ok;
}

AssetResult sendAsset(httpd_req_t *req, const char *uri) {
    bool gzip = true;
    AssetFile file = openVariant(uri, gzip);
    if (!file) {
        gzip = false;
        file = openVariant(uri, gzip);
    }
    if (!file)
        return AssetResult::Missing;
    const bool sent = setHeaders(req, uri, gzip) && streamFile(req, file.get());
    return sent ? AssetResult::Sent : AssetResult::Broken;
}

esp_err_t finish(httpd_req_t *req, AssetResult result) {
    if (result == AssetResult::Missing)
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, nullptr);
    if (result == AssetResult::Broken)
        LOG_WARN("WORKSHOP", "Tuner asset %s was cut short", req->uri);
    return result == AssetResult::Sent ? ESP_OK : ESP_FAIL;
}

}

esp_err_t handleTunerAsset(httpd_req_t *req) {
    const AssetResult direct = sendAsset(req, req->uri);
    if (direct != AssetResult::Missing || !workshop_tuner_spa_route_rs(req->uri))
        return finish(req, direct);
    return finish(req, sendAsset(req, kIndexUri));
}

}

#endif
