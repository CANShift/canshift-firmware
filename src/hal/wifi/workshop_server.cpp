#include "app_config.h"
#if APP_WORKSHOP_ENABLED

    #include "workshop_server.h"
    #include "workshop_server_internal.h"

    #include "diag/error_store.h"
    #include "diag/heap_stats.h"
    #include "diag/logger.h"
    #include "runtime/task_spawn.h"
    #include "runtime/workshop_credentials.h"

    #include <ESPmDNS.h>
    #include <WiFi.h>
    #include <esp_task_wdt.h>
    #include <freertos/FreeRTOS.h>
    #include <freertos/task.h>

namespace WorkshopServer {

namespace {

constexpr uint8_t kChannel = 6;
constexpr uint8_t kMaxStations = 2;
constexpr bool kHiddenNetwork = false;
constexpr char kHostName[] = "canshift";
constexpr uint16_t kHttpPort = 80;
constexpr uint32_t kStartStackBytes = 8192;
constexpr UBaseType_t kStartPriority = 2;
constexpr BaseType_t kStartCore = 0;

bool startAccessPoint(const WorkshopCredentials &creds) {
    const bool up = WiFi.mode(WIFI_AP) &&
                    WiFi.softAP(creds.ssid, creds.password, kChannel, kHiddenNetwork, kMaxStations);
    if (up)
        LOG_INFO("WORKSHOP", "Access point %s up at %s", creds.ssid,
                 WiFi.softAPIP().toString().c_str());
    return up;
}

void startMdns() {
    if (!MDNS.begin(kHostName) || !MDNS.addService(String("http"), String("tcp"), kHttpPort)) {
        LOG_WARN("WORKSHOP", "mDNS unavailable — the tuner is at http://%s",
                 WiFi.softAPIP().toString().c_str());
        return;
    }
    LOG_INFO("WORKSHOP", "Answering as http://%s.local", kHostName);
}

void start() {
    HeapStats::logMoment("workshop before Wi-Fi");
    WorkshopCredentials creds{};
    const bool up = WorkshopCredentialStore::loadOrCreate(creds) && startAccessPoint(creds) &&
                    WorkshopServerInternal::startHttp();
    if (!up) {
        LOG_ERROR("WORKSHOP", "Access point or tuner server failed to start");
        ErrorStore::push(ERROR_SRC_SYSTEM, "workshop", "access point not started");
        return;
    }
    startMdns();
    HeapStats::logMoment("workshop server up");
}

void runStart(void *) {
    start();
    if (esp_task_wdt_delete(nullptr) != ESP_OK)
        LOG_WARN("WORKSHOP", "Start task was not watched by the WDT");
    vTaskDelete(nullptr);
}

} // namespace

void requestStart() {
    if (!TaskSpawn::spawnDynamic("workshop", runStart, kStartStackBytes, kStartPriority,
                                 kStartCore))
        LOG_ERROR("WORKSHOP", "Workshop start task could not be created");
}

void tick() {
    if (WorkshopServerInternal::socketBound())
        WorkshopServerInternal::drainSocket();
}

} // namespace WorkshopServer

#endif
