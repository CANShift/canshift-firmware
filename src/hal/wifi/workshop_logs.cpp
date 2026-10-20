#include "app_config.h"
#if APP_WORKSHOP_ENABLED && APP_SD_DATALOG_ENABLED

    #include "workshop_server_internal.h"

    #include "datalog_rs.h"
    #include "diag/logger.h"
    #include "hal/storage/sd_card.h"

    #include <dirent.h>
    #include <errno.h>
    #include <stdio.h>
    #include <string.h>
    #include <sys/stat.h>
    #include <unistd.h>

namespace WorkshopServerInternal {

namespace {

constexpr char kJsonType[] = "application/json";
constexpr char kBinaryType[] = "application/octet-stream";
constexpr char kFileUriPrefix[] = "/api/logs/";
constexpr char kNoCardIndex[] = "{\"card\":false,\"logs\":[]}";
constexpr char kIndexOpen[] = "{\"card\":true,\"logs\":[";
constexpr char kIndexClose[] = "]}";
constexpr size_t kPathCapacity = 48;
constexpr size_t kEntryCapacity = 64;
constexpr size_t kDispositionCapacity = 48;
constexpr int kNameWidth = static_cast<int>(kDatalogNameBytes);

struct DirCloser {
    void operator()(DIR *dir) const {
        if (closedir(dir) != 0)
            LOG_WARN("WORKSHOP", "log directory close failed");
    }
};

using LogDir = std::unique_ptr<DIR, DirCloser>;

bool isLogName(const char *name) {
    return datalog_is_log_name_rs(reinterpret_cast<const uint8_t *>(name), strlen(name));
}

const char *logNameFromUri(const char *uri) {
    const size_t prefix = sizeof(kFileUriPrefix) - 1;
    if (strncmp(uri, kFileUriPrefix, prefix) != 0)
        return nullptr;
    return isLogName(uri + prefix) ? uri + prefix : nullptr;
}

void logPath(const char *name, char *out, size_t capacity) {
    snprintf(out, capacity, "%s/%.*s", SdCard::kLogDir, kNameWidth, name);
}

uint32_t logBytes(const char *name) {
    char path[kPathCapacity];
    logPath(name, path, sizeof(path));
    struct stat info{};
    return stat(path, &info) == 0 ? static_cast<uint32_t>(info.st_size) : 0;
}

bool sendText(httpd_req_t *req, const char *text) {
    return httpd_resp_send_chunk(req, text, static_cast<ssize_t>(strlen(text))) == ESP_OK;
}

bool sendEntries(httpd_req_t *req, DIR *dir) {
    bool first = true;
    while (const dirent *entry = readdir(dir)) {
        if (!isLogName(entry->d_name))
            continue;
        char json[kEntryCapacity];
        snprintf(json, sizeof(json), "%s{\"name\":\"%.*s\",\"bytes\":%u}", first ? "" : ",",
                 kNameWidth, entry->d_name, static_cast<unsigned>(logBytes(entry->d_name)));
        if (!sendText(req, json))
            return false;
        first = false;
    }
    return true;
}

bool sendIndex(httpd_req_t *req) {
    LogDir dir(opendir(SdCard::kLogDir));
    if (!sendText(req, kIndexOpen))
        return false;
    if (dir && !sendEntries(req, dir.get()))
        return false;
    return sendText(req, kIndexClose) && httpd_resp_send_chunk(req, nullptr, 0) == ESP_OK;
}

HttpFile openLog(const char *name) {
    if (name == nullptr || !SdCard::mounted())
        return nullptr;
    char path[kPathCapacity];
    logPath(name, path, sizeof(path));
    return HttpFile(fopen(path, "rb"));
}

esp_err_t replyDeleteFailure(httpd_req_t *req, const char *name) {
    if (errno == ENOENT)
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, nullptr);
    LOG_WARN("WORKSHOP", "log %s not deleted: errno %d", name, errno);
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, nullptr);
}

}

esp_err_t handleLogIndex(httpd_req_t *req) {
    if (!admitTunerRequest(req))
        return ESP_OK;
    if (httpd_resp_set_type(req, kJsonType) != ESP_OK || !setSecurityHeaders(req))
        return ESP_FAIL;
    if (!SdCard::mounted())
        return httpd_resp_sendstr(req, kNoCardIndex);
    return sendIndex(req) ? ESP_OK : ESP_FAIL;
}

esp_err_t handleLogDownload(httpd_req_t *req) {
    if (!admitTunerRequest(req))
        return ESP_OK;
    const char *name = logNameFromUri(req->uri);
    HttpFile file = openLog(name);
    if (!file)
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, nullptr);
    char disposition[kDispositionCapacity];
    snprintf(disposition, sizeof(disposition), "attachment; filename=\"%.*s\"", kNameWidth, name);
    const bool sent = httpd_resp_set_type(req, kBinaryType) == ESP_OK && setSecurityHeaders(req) &&
                      httpd_resp_set_hdr(req, "Content-Disposition", disposition) == ESP_OK &&
                      streamFile(req, file.get());
    if (!sent)
        LOG_WARN("WORKSHOP", "log %s download was cut short", name);
    return sent ? ESP_OK : ESP_FAIL;
}

esp_err_t handleLogDelete(httpd_req_t *req) {
    if (!admitTunerRequest(req))
        return ESP_OK;
    const char *name = logNameFromUri(req->uri);
    if (name == nullptr || !SdCard::mounted())
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, nullptr);
    char path[kPathCapacity];
    logPath(name, path, sizeof(path));
    if (unlink(path) != 0)
        return replyDeleteFailure(req, name);
    LOG_INFO("WORKSHOP", "log %s deleted", name);
    if (!setSecurityHeaders(req) || httpd_resp_set_status(req, "204 No Content") != ESP_OK)
        return ESP_FAIL;
    return httpd_resp_send(req, nullptr, 0);
}

}

#endif
