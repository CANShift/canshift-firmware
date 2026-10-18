#pragma once

#include <esp_http_server.h>

namespace WorkshopServerInternal {

inline constexpr char kHostName[] = "canshift";

[[nodiscard]] bool startHttp();

esp_err_t handleTunerAsset(httpd_req_t *req);

esp_err_t handleSocket(httpd_req_t *req);

void onSessionClosed(httpd_handle_t server, int sockfd);

[[nodiscard]] bool bindSocket(httpd_handle_t server);

[[nodiscard]] bool socketBound();

void drainSocket();

}
