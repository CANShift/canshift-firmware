#include "app_config.h"
#if APP_WORKSHOP_ENABLED

    #include "workshop_server_internal.h"

    #include "diag/logger.h"
    #include "workshop_mode_rs.h"

    #include <esp_http_server.h>

    #include <memory>
    #include <stdint.h>
    #include <stdio.h>

namespace WorkshopServerInternal {

namespace {

constexpr char kMountPoint[] = "/spiffs";
constexpr char kIndexUri[] = "/";
constexpr char kGzipEncoding[] = "gzip";
constexpr size_t kChunkBytes = 2048;
constexpr size_t kFullPathCap = sizeof(kMountPoint) + WORKSHOP_TUNER_PATH_CAP;

struct ResponseHeader {
    const char *name;
    const char *value;
};

constexpr ResponseHeader kResponseHeaders[] = {
    {"Content-Security-Policy", "default-src 'self'"},
    {"X-Content-Type-Options", "nosniff"},
    {"X-Frame-Options", "DENY"},
    {"Referrer-Policy", "no-referrer"},
    {"Cache-Control", "no-cache"},
};

enum class AssetResult : uint8_t { Sent, Missing, Broken };

struct FileCloser {
    void operator()(FILE *file) const {
        if (fclose(file) != 0)
            LOG_WARN("WORKSHOP", "Tuner asset close failed");
    }
};

using AssetFile = std::unique_ptr<FILE, FileCloser>;

AssetFile openVariant(const char *uri, bool gzip) {
    char path[WORKSHOP_TUNER_PATH_CAP];
    if (workshop_tuner_path_rs(uri, gzip, path, sizeof(path)) == 0)
        return nullptr;
    char fullPath[kFullPathCap];
    snprintf(fullPath, sizeof(fullPath), "%s%s", kMountPoint, path);
    return AssetFile(fopen(fullPath, "rb"));
}

bool setHeaders(httpd_req_t *req, const char *uri, bool gzip) {
    bool ok = httpd_resp_set_type(req, workshop_tuner_content_type_rs(uri)) == ESP_OK;
    for (const ResponseHeader &header : kResponseHeaders)
        ok = ok && httpd_resp_set_hdr(req, header.name, header.value) == ESP_OK;
    if (gzip)
        ok = ok && httpd_resp_set_hdr(req, "Content-Encoding", kGzipEncoding) == ESP_OK;
    return ok;
}

bool streamFile(httpd_req_t *req, FILE *file) {
    char chunk[kChunkBytes];
    size_t read = 0;
    while ((read = fread(chunk, 1, sizeof(chunk), file)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, static_cast<ssize_t>(read)) != ESP_OK)
            return false;
    }
    return httpd_resp_send_chunk(req, nullptr, 0) == ESP_OK;
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

} // namespace

esp_err_t handleTunerAsset(httpd_req_t *req) {
    const AssetResult direct = sendAsset(req, req->uri);
    if (direct != AssetResult::Missing || !workshop_tuner_spa_route_rs(req->uri))
        return finish(req, direct);
    return finish(req, sendAsset(req, kIndexUri));
}

} // namespace WorkshopServerInternal

#endif
