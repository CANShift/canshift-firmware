#pragma once

#include "workshop_mode_rs.h"

struct WorkshopCredentials {
    char ssid[kWorkshopSsidCap];
    char password[kWorkshopPasswordCap];
};

namespace WorkshopCredentialStore {

[[nodiscard]] bool loadOrCreate(WorkshopCredentials &out);

}
