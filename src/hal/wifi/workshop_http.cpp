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
    #if APP_SD_DATALOG_ENABLED
constexpr char kLogIndexUri[] = "/api/logs";
constexpr char kLogFileUri[] = "/api/logs/*";
    #endif

httpd_handle_t s_server = nullptr;

constexpr httpd_uri_t route(const char *uri, httpd_method_t method,
                            esp_err_t (*handler)(httpd_req_t *), bool websocket) {
    return {
        .uri = uri,
        .method = method,
        .handler = handler,
        .user_ctx = nullptr,
        .is_websocket = websocket,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
    };
}

const httpd_uri_t kRoutes[] = {
    route(kSocketUri, HTTP_GET, handleSocket, true),
    #if APP_SD_DATALOG_ENABLED
    route(kLogIndexUri, HTTP_GET, handleLogIndex, false),
    route(kLogFileUri, HTTP_GET, handleLogDownload, false),
    route(kLogFileUri, HTTP_DELETE, handleLogDelete, false),
    #endif
    route(kAssetUri, HTTP_GET, handleTunerAsset, false),
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
    for (const httpd_uri_t &entry : kRoutes) {
        if (httpd_register_uri_handler(s_server, &entry) != ESP_OK) {
            LOG_ERROR("WORKSHOP", "HTTP route %s not registered", entry.uri);
            return false;
        }
    }
    return true;
}

}

bool startHttp() {
    httpd_config_t config = serverConfig();
    const esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        LOG_ERROR("WORKSHOP", "HTTP server failed to start: %s", esp_err_to_name(err));
        return false;
    }
    return registerRoutes() && bindSocket(s_server);
}

}

#endif
