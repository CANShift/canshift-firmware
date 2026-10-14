#pragma once

#include "workshop_mode_rs.h"

struct WorkshopCredentials {
    char ssid[WORKSHOP_SSID_CAP];
    char password[WORKSHOP_PASSWORD_CAP];
};

namespace WorkshopCredentialStore {

[[nodiscard]] bool loadOrCreate(WorkshopCredentials &out);

}
