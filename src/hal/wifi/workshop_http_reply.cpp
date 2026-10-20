#include "app_config.h"
#if APP_WORKSHOP_ENABLED

    #include "workshop_server_internal.h"

    #include "diag/logger.h"

    #include <stdio.h>

namespace WorkshopServerInternal {

namespace {

constexpr size_t kChunkBytes = 2048;

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

}

void FileCloser::operator()(FILE *file) const {
    if (fclose(file) != 0)
        LOG_WARN("WORKSHOP", "HTTP file close failed");
}

bool setSecurityHeaders(httpd_req_t *req) {
    bool ok = true;
    for (const ResponseHeader &header : kResponseHeaders)
        ok = ok && httpd_resp_set_hdr(req, header.name, header.value) == ESP_OK;
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

}

#endif
