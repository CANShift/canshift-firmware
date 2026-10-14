#include "app_config.h"
#if APP_WORKSHOP_ENABLED

    #include "workshop_server_internal.h"

    #include "diag/logger.h"

    #include <esp_http_server.h>

namespace WorkshopServerInternal {

namespace {

constexpr size_t kHttpStackBytes = 6144;
constexpr uint16_t kMaxOpenSockets = 4;
constexpr uint16_t kSendWaitSeconds = 1;
constexpr char kSocketUri[] = "/ws";
constexpr char kAssetUri[] = "/*";

httpd_handle_t s_server = nullptr;

const httpd_uri_t kSocketRoute = {
    .uri = kSocketUri,
    .method = HTTP_GET,
    .handler = handleSocket,
    .user_ctx = nullptr,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = nullptr,
};

const httpd_uri_t kAssetRoute = {
    .uri = kAssetUri,
    .method = HTTP_GET,
    .handler = handleTunerAsset,
    .user_ctx = nullptr,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = nullptr,
};

httpd_config_t serverConfig() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = kHttpStackBytes;
    config.max_open_sockets = kMaxOpenSockets;
    config.lru_purge_enable = true;
    config.send_wait_timeout = kSendWaitSeconds;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.close_fn = onSessionClosed;
    return config;
}

bool registerRoutes() {
    return httpd_register_uri_handler(s_server, &kSocketRoute) == ESP_OK &&
           httpd_register_uri_handler(s_server, &kAssetRoute) == ESP_OK;
}

} // namespace

bool startHttp() {
    httpd_config_t config = serverConfig();
    const esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        LOG_ERROR("WORKSHOP", "HTTP server failed to start: %s", esp_err_to_name(err));
        return false;
    }
    return registerRoutes() && bindSocket(s_server);
}

} // namespace WorkshopServerInternal

#endif
