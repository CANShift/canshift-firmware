#include "app_config.h"
#if APP_WORKSHOP_ENABLED

    #include "workshop_server_internal.h"

    #include "diag/logger.h"
    #include "workshop_mode_rs.h"

    #include <Arduino.h>
    #include <WiFi.h>
    #include <esp_http_server.h>
    #include <lwip/sockets.h>

    #include <string.h>

namespace WorkshopServerInternal {

namespace {

constexpr char kOriginHeader[] = "Origin";
constexpr size_t kOriginCap = 64;

enum class Verdict : uint8_t { Allowed, ForeignOrigin, NotOwner };

struct Refusal {
    const char *status;
    const char *reason;
};

constexpr Refusal kRefusals[] = {
    {"", ""},
    {"403 Forbidden", "foreign Origin"},
    {"409 Conflict", "not the tuner holding the socket"},
};

static_assert(sizeof(kRefusals) / sizeof(kRefusals[0]) ==
                  static_cast<size_t>(Verdict::NotOwner) + 1,
              "every verdict needs a refusal");

bool peerOf(int fd, sockaddr_storage &peer) {
    socklen_t len = sizeof(peer);
    return getpeername(fd, reinterpret_cast<sockaddr *>(&peer), &len) == 0;
}

bool sameHost(const sockaddr_storage &a, const sockaddr_storage &b) {
    if (a.ss_family != b.ss_family)
        return false;
    if (a.ss_family == AF_INET)
        return reinterpret_cast<const sockaddr_in &>(a).sin_addr.s_addr ==
               reinterpret_cast<const sockaddr_in &>(b).sin_addr.s_addr;
    return memcmp(&reinterpret_cast<const sockaddr_in6 &>(a).sin6_addr,
                  &reinterpret_cast<const sockaddr_in6 &>(b).sin6_addr, sizeof(in6_addr)) == 0;
}

bool fromSocketOwner(httpd_req_t *req) {
    const int owner = socketOwnerFd();
    if (owner < 0)
        return false;
    sockaddr_storage ownerPeer{};
    sockaddr_storage requestPeer{};
    return peerOf(owner, ownerPeer) && peerOf(httpd_req_to_sockfd(req), requestPeer) &&
           sameHost(ownerPeer, requestPeer);
}

Verdict judge(httpd_req_t *req) {
    if (!originAllowed(req))
        return Verdict::ForeignOrigin;
    return fromSocketOwner(req) ? Verdict::Allowed : Verdict::NotOwner;
}

}

bool originAllowed(httpd_req_t *req) {
    const size_t len = httpd_req_get_hdr_value_len(req, kOriginHeader);
    if (len == 0)
        return workshop_origin_allowed_rs(nullptr, nullptr, kHostName);
    char origin[kOriginCap];
    if (len >= sizeof(origin) ||
        httpd_req_get_hdr_value_str(req, kOriginHeader, origin, sizeof(origin)) != ESP_OK)
        return false;
    const String apIp = WiFi.softAPIP().toString();
    return workshop_origin_allowed_rs(origin, apIp.c_str(), kHostName);
}

bool admitTunerRequest(httpd_req_t *req) {
    const Verdict verdict = judge(req);
    if (verdict == Verdict::Allowed)
        return true;
    const Refusal &refusal = kRefusals[static_cast<size_t>(verdict)];
    LOG_WARN("WORKSHOP", "%s %s refused: %s",
             http_method_str(static_cast<httpd_method_t>(req->method)), req->uri, refusal.reason);
    if (httpd_resp_set_status(req, refusal.status) != ESP_OK || !setSecurityHeaders(req) ||
        httpd_resp_send(req, nullptr, 0) != ESP_OK)
        LOG_WARN("WORKSHOP", "refusal for %s not sent", req->uri);
    return false;
}

}

#endif
