#pragma once

#include "app_config.h"

#include <esp_http_server.h>

#include <memory>
#include <stdio.h>

namespace WorkshopServerInternal {

inline constexpr char kHostName[] = "canshift";

[[nodiscard]] bool startHttp();

struct FileCloser {
    void operator()(FILE *file) const;
};

using HttpFile = std::unique_ptr<FILE, FileCloser>;

[[nodiscard]] bool setSecurityHeaders(httpd_req_t *req);

[[nodiscard]] bool streamFile(httpd_req_t *req, FILE *file);

[[nodiscard]] bool originAllowed(httpd_req_t *req);

[[nodiscard]] bool admitTunerRequest(httpd_req_t *req);

[[nodiscard]] int socketOwnerFd();

esp_err_t handleTunerAsset(httpd_req_t *req);

#if APP_SD_DATALOG_ENABLED
esp_err_t handleLogIndex(httpd_req_t *req);

esp_err_t handleLogDownload(httpd_req_t *req);

esp_err_t handleLogDelete(httpd_req_t *req);
#endif

esp_err_t handleSocket(httpd_req_t *req);

void onSessionClosed(httpd_handle_t server, int sockfd);

[[nodiscard]] bool bindSocket(httpd_handle_t server);

[[nodiscard]] bool socketBound();

void drainSocket();

}
