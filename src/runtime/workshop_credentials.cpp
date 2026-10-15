#include "app_config.h"
#if APP_WORKSHOP_ENABLED

    #include "workshop_credentials.h"

    #include "diag/error_store.h"
    #include "diag/logger.h"
    #include "hal/storage/nvs_store.h"

    #include <Preferences.h>
    #include <esp_system.h>
    #include <stdint.h>
    #include <string.h>

namespace WorkshopCredentialStore {

namespace {

constexpr char kNamespace[] = "workshop";
constexpr char kSsidKey[] = "ssid";
constexpr char kPasswordKey[] = "pass";
constexpr size_t kRandomWordBytes = sizeof(uint32_t);

void readString(Preferences &prefs, const char *key, char *out, size_t cap) {
    const String value = prefs.getString(key, "");
    strlcpy(out, value.c_str(), cap);
}

bool load(WorkshopCredentials &out) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, true))
        return false;
    readString(prefs, kSsidKey, out.ssid, sizeof(out.ssid));
    readString(prefs, kPasswordKey, out.password, sizeof(out.password));
    prefs.end();
    return workshop_credentials_valid_rs(out.ssid, out.password);
}

void fillEntropy(uint8_t *out, size_t len) {
    for (size_t i = 0; i < len; i += kRandomWordBytes) {
        const uint32_t word = esp_random();
        memcpy(out + i, &word, kRandomWordBytes);
    }
}

bool generate(WorkshopCredentials &out) {
    static_assert(kWorkshopEntropyBytes % kRandomWordBytes == 0,
                  "entropy is drawn a word at a time");
    uint8_t entropy[kWorkshopEntropyBytes];
    fillEntropy(entropy, sizeof(entropy));
    return workshop_generate_credentials_rs(entropy, sizeof(entropy), out.ssid, sizeof(out.ssid),
                                            out.password, sizeof(out.password));
}

bool persist(const WorkshopCredentials &creds) {
    return NvsStore::putString(kNamespace, kSsidKey, creds.ssid, strlen(creds.ssid)) &&
           NvsStore::putString(kNamespace, kPasswordKey, creds.password, strlen(creds.password));
}

bool create(WorkshopCredentials &out) {
    if (!generate(out) || !persist(out)) {
        LOG_ERROR("WORKSHOP", "Network credentials could not be created and stored");
        ErrorStore::push(ERROR_SRC_SYSTEM, "workshop", "network credentials not stored");
        return false;
    }
    LOG_INFO("WORKSHOP", "Network credentials created for %s", out.ssid);
    return true;
}

} // namespace

bool loadOrCreate(WorkshopCredentials &out) {
    if (load(out))
        return true;
    return create(out);
}

} // namespace WorkshopCredentialStore

#endif
